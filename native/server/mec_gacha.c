/* mec_gacha.c - 抽卡记录：游戏日志解析 + 官方接口同步（纯 C + 系统 curl，零第三方依赖）
 *
 * 链路复刻自开源项目 juliy819/wuwa-gacha-tool (Apache License 2.0)：
 *   1. Client.log 加密格式：跳过前 3 字节 BOM，逐字节 XOR（奇数字节 ^0xA5，偶数字节 ^0xEF）
 *   2. 从 "OpenWebView ... sdkJson" 行提取唤取记录链接（\u0026 还原为 &，按时间戳取最新）
 *   3. 解析 player_id / record_id / resources_id / svr_id / lang 参数
 *   4. 按 13 种卡池逐一 POST 官方接口（无分页，一次返回该卡池全量），合并写入 gacha_data.json
 */
#include "mec_gacha.h"
#include "mec_config.h"
#include "win_utf8.h"
#include "mec_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <windows.h>

/* ---------------- 常量 ---------------- */

#define GACHA_URL_PREFIX "https://aki-gm-resources.aki-game.com/aki/gacha/index.html"
#define CN_API_URL     "https://gmserver-api.aki-game2.com/gacha/record/query"
#define GLOBAL_API_URL "https://gmserver-api.aki-game2.net/gacha/record/query"

/* 卡池类型 ID（1~13，与上游 POOL_TYPES 一致） */
static const char* POOL_IDS[13] = { "1","2","3","4","5","6","7","8","9","10","11","12","13" };

typedef struct {
    char player_id[64];
    char record_id[96];
    char resources_id[96];
    char svr_id[96];
    char lang[32];
} GachaParams;

/* ---------------- 小工具 ---------------- */

/* 整个文件读入内存（UTF-8 路径），返回 malloc 缓冲与长度；失败返回 NULL */
static char* gacha_read_file(const char* path, size_t* out_len) {
    FILE* f = mec_fopen_utf8(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *out_len = got;
    return buf;
}

/* 校验参数只含安全字符（参数会被拼进 JSON 请求体，白名单防注入） */
static int is_safe_token(const char* s) {
    if (!s || !s[0]) return 0;
    for (const char* p = s; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.') continue;
        return 0;
    }
    return 1;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 从 query 片段 [qs,qe) 中取 key=val（百分号解码），找到返回 1 */
static int query_get(const char* qs, const char* qe, const char* key, char* out, int out_size) {
    size_t klen = strlen(key);
    const char* i = qs;
    while (i < qe) {
        const char* amp = i;
        while (amp < qe && *amp != '&') amp++;
        const char* eq = i;
        while (eq < amp && *eq != '=') eq++;
        if ((size_t)(eq - i) == klen && memcmp(i, key, klen) == 0) {
            const char* vs = eq + 1;
            int n = 0;
            for (const char* p = vs; p < amp; p++) {
                char c = *p;
                if (c == '+') c = ' ';
                else if (c == '%' && p + 2 < amp) {
                    int hi = hexval(p[1]), lo = hexval(p[2]);
                    if (hi >= 0 && lo >= 0) { c = (char)(hi * 16 + lo); p += 2; }
                }
                if (n >= out_size - 1) break;
                out[n++] = c;
            }
            out[n] = 0;
            return 1;
        }
        i = amp + 1;
    }
    return 0;
}

/* 把日志 URL 里的字面 \u0026 全部还原成 &（就地） */
static void normalize_logged_url(char* url) {
    char* r = url;
    char* w = url;
    while (*r) {
        if (strncmp(r, "\\u0026", 6) == 0) { *w++ = '&'; r += 6; }
        else { *w++ = *r++; }
    }
    *w = 0;
}

/* 校验是唤取记录页链接：前缀精确 + 带 #/record（排除同站公告页等） */
static int is_gacha_record_url(const char* url) {
    size_t plen = strlen(GACHA_URL_PREFIX);
    if (strncmp(url, GACHA_URL_PREFIX, plen) != 0) return 0;
    char next = url[plen];
    if (next != 0 && next != '?' && next != '#') return 0; /* 防仿冒前缀 */
    return strstr(url, "#/record") != NULL;
}

/* 从链接解析请求参数（hash 参数优先于顶层 query，兼容云鸣潮双份参数格式） */
static int parse_gacha_params(const char* url, GachaParams* p, char* err, int err_size) {
    memset(p, 0, sizeof(*p));
    const char* hash = strchr(url, '#');
    const char* qmark = strchr(url, '?');

    /* 顶层 query（# 之前）先写入 */
    if (qmark && (!hash || qmark < hash)) {
        const char* qs = qmark + 1;
        const char* qe = hash ? hash : url + strlen(url);
        query_get(qs, qe, "player_id", p->player_id, sizeof(p->player_id));
        query_get(qs, qe, "record_id", p->record_id, sizeof(p->record_id));
        query_get(qs, qe, "resources_id", p->resources_id, sizeof(p->resources_id));
        query_get(qs, qe, "svr_id", p->svr_id, sizeof(p->svr_id));
        query_get(qs, qe, "lang", p->lang, sizeof(p->lang));
    }
    /* fragment query（#/record?...）覆盖，优先级更高 */
    if (hash) {
        const char* fq = strchr(hash, '?');
        if (fq) {
            const char* qs = fq + 1;
            const char* qe = url + strlen(url);
            query_get(qs, qe, "player_id", p->player_id, sizeof(p->player_id));
            query_get(qs, qe, "record_id", p->record_id, sizeof(p->record_id));
            query_get(qs, qe, "resources_id", p->resources_id, sizeof(p->resources_id));
            query_get(qs, qe, "svr_id", p->svr_id, sizeof(p->svr_id));
            query_get(qs, qe, "lang", p->lang, sizeof(p->lang));
        }
    }

    if (!p->player_id[0] || !p->record_id[0]) {
        if (err) snprintf(err, err_size, "URL 参数不完整（缺少 player_id 或 record_id）");
        return -1;
    }
    if (!is_safe_token(p->player_id) || !is_safe_token(p->record_id) ||
        (p->resources_id[0] && !is_safe_token(p->resources_id)) ||
        (p->svr_id[0] && !is_safe_token(p->svr_id)) ||
        (p->lang[0] && !is_safe_token(p->lang))) {
        if (err) snprintf(err, err_size, "URL 参数包含非法字符");
        return -1;
    }
    if (!p->lang[0]) snprintf(p->lang, sizeof(p->lang), "zh-Hans");
    return 0;
}

/* ---------------- 游戏日志扫描 ---------------- */

/* 多路径回退定位 Client.log（上游 resolve_log_path 的简化版） */
static int resolve_log_path(const char* game_dir, char* out, int out_size) {
    char cand[4200];
    snprintf(cand, sizeof(cand), "%s\\Client\\Saved\\Logs\\Client.log", game_dir);
    if (mec_file_exists(cand)) { snprintf(out, out_size, "%s", cand); return 0; }
    snprintf(cand, sizeof(cand), "%s\\Saved\\Logs\\Client.log", game_dir);
    if (mec_file_exists(cand)) { snprintf(out, out_size, "%s", cand); return 0; }
    snprintf(cand, sizeof(cand), "%s\\Logs\\Client.log", game_dir);
    if (mec_file_exists(cand)) { snprintf(out, out_size, "%s", cand); return 0; }
    snprintf(cand, sizeof(cand), "%s\\Client.log", game_dir);
    if (mec_file_exists(cand)) { snprintf(out, out_size, "%s", cand); return 0; }
    return -1;
}

/* 在行缓冲 [line,line+len) 内查找子串 */
static int line_has(const char* line, size_t len, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen > len) return 0;
    for (size_t i = 0; i + nlen <= len; i++)
        if (memcmp(line + i, needle, nlen) == 0) return 1;
    return 0;
}

/* 在解码日志中提取最新的唤取记录链接 */
static int extract_gacha_url(const char* decoded, char* out_url, int out_size) {
    char best_url[2048] = { 0 };
    char best_time[40] = { 0 };
    const char* line = decoded;

    while (*line) {
        const char* nl = strchr(line, '\n');
        size_t llen = nl ? (size_t)(nl - line) : strlen(line);
        const char* line_end = line + llen;

        if (llen > 24 && line[0] == '[' && llen >= 26 &&
            line_has(line, llen, "OpenWebView")) {
            /* 行内找 sdkJson 与 "url":" */
            int has_sdk = 0;
            for (const char* q = line; q + 7 <= line_end; q++) {
                if (memcmp(q, "sdkJson", 7) == 0) { has_sdk = 1; break; }
            }
            if (has_sdk) {
                const char* us = NULL;
                for (const char* q = line; q + 7 <= line_end; q++) {
                    if (memcmp(q, "\"url\":\"", 7) == 0) { us = q + 7; break; }
                }
                if (us) {
                    const char* ue = NULL;
                    for (const char* q = us; q < line_end; q++) if (*q == '"') { ue = q; break; }
                    if (ue) {
                        size_t ulen = (size_t)(ue - us);
                        if (ulen < sizeof(best_url)) {
                            char url[2048];
                            memcpy(url, us, ulen);
                            url[ulen] = 0;
                            normalize_logged_url(url);
                            if (is_gacha_record_url(url)) {
                                /* 时间戳 [YYYY.MM.DD-HH.MM.SS:mmm]，固定宽度，字典序可比较 */
                                char ts[40] = { 0 };
                                const char* rb = memchr(line, ']', llen);
                                if (rb && rb > line + 1 && (size_t)(rb - line) < sizeof(ts)) {
                                    memcpy(ts, line + 1, (size_t)(rb - line - 1));
                                }
                                if (strcmp(ts, best_time) >= 0) {
                                    snprintf(best_time, sizeof(best_time), "%s", ts);
                                    snprintf(best_url, sizeof(best_url), "%s", url);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!nl) break;
        line = nl + 1;
    }

    /* 回退：宽松扫描任意位置的记录页链接 */
    if (!best_url[0]) {
        const char* p = decoded;
        while ((p = strstr(p, GACHA_URL_PREFIX)) != NULL) {
            const char* e = p;
            while (*e && *e != '"' && *e != ' ' && *e != '\n' && *e != '\r') e++;
            size_t ulen = (size_t)(e - p);
            if (ulen < sizeof(best_url)) {
                memcpy(best_url, p, ulen);
                best_url[ulen] = 0;
                normalize_logged_url(best_url);
                if (is_gacha_record_url(best_url)) break;
                best_url[0] = 0;
            }
            p = e;
        }
    }

    if (!best_url[0]) return -1;
    snprintf(out_url, out_size, "%s", best_url);
    return 0;
}

/* 读/写 gacha_config.json 中保存的游戏目录 */
static void gacha_config_path(const char* root, char* out, int out_size) {
    snprintf(out, out_size, "%s%cgacha_config.json", root ? root : ".", '\\');
}

static void load_game_dir(const char* root, char* out, int out_size) {
    out[0] = 0;
    char path[2200];
    gacha_config_path(root, path, sizeof(path));
    size_t len = 0;
    char* buf = gacha_read_file(path, &len);
    if (!buf) return;
    mec_json_get_string(buf, "game_dir", out, out_size);
    /* 还原 JSON 转义的 \\ */
    char* r = out; char* w = out;
    while (*r) { if (r[0] == '\\' && r[1] == '\\') r++; *w++ = *r++; }
    *w = 0;
    free(buf);
}

static void save_game_dir(const char* root, const char* game_dir) {
    char path[2200];
    gacha_config_path(root, path, sizeof(path));
    char esc[4200]; int e = 0;
    for (const char* c = game_dir; *c && e < (int)sizeof(esc) - 2; c++) {
        if (*c == '\\') esc[e++] = '\\';
        esc[e++] = *c;
    }
    esc[e] = 0;
    char out[8600];
    int n = snprintf(out, sizeof(out), "{\n  \"game_dir\": \"%s\"\n}\n", esc);
    mec_write_file_utf8(path, out, (size_t)n);
}

int mec_gacha_scan(const char* root, const char* game_dir, char* out_url, int out_size, char* err, int err_size) {
    char dir[2048];
    if (!game_dir || !game_dir[0]) {
        load_game_dir(root, dir, sizeof(dir)); /* 用上次保存的目录 */
    } else {
        snprintf(dir, sizeof(dir), "%s", game_dir);
    }
    if (!dir[0]) {
        snprintf(err, err_size, "请先填写鸣潮游戏安装目录");
        return -1;
    }
    mec_trim(dir);
    size_t dl = strlen(dir);
    while (dl > 0 && (dir[dl - 1] == '\\' || dir[dl - 1] == '/')) dir[--dl] = 0;
    for (char* p = dir; *p; p++) if (*p == '/') *p = '\\';

    char log_path[4300];
    if (resolve_log_path(dir, log_path, sizeof(log_path)) != 0) {
        snprintf(err, err_size, "未找到 Client.log（请确认目录是鸣潮安装根目录）");
        return -1;
    }

    /* 共享读打开：游戏运行时会持续写该文件 */
    wchar_t wp[2600];
    if (!utf8_to_wide(log_path, wp, 2600)) { snprintf(err, err_size, "日志路径无效"); return -1; }
    HANDLE h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { snprintf(err, err_size, "无法打开 Client.log"); return -1; }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 64 || sz.QuadPart > (LONGLONG)64 * 1024 * 1024) {
        CloseHandle(h);
        snprintf(err, err_size, "Client.log 大小异常");
        return -1;
    }
    char* raw = (char*)malloc((size_t)sz.QuadPart + 1);
    DWORD got = 0;
    if (!raw || !ReadFile(h, raw, (DWORD)sz.QuadPart, &got, NULL) || got < 64) {
        free(raw); CloseHandle(h);
        snprintf(err, err_size, "读取 Client.log 失败");
        return -1;
    }
    CloseHandle(h);
    raw[got] = 0;

    /* 解密（就地）：跳过 3 字节 BOM，逐字节 XOR */
    size_t n = 0;
    for (size_t i = 3; i < (size_t)got; i++) {
        unsigned char b = (unsigned char)raw[i];
        raw[n++] = (char)((b % 2 == 1) ? (b ^ 0xA5) : (b ^ 0xEF));
    }
    for (size_t i = 0; i < n; i++) if (raw[i] == 0) raw[i] = ' ';
    raw[n] = 0;

    int rc = extract_gacha_url(raw, out_url, out_size);
    free(raw);
    if (rc != 0) {
        snprintf(err, err_size, "日志中没有唤取记录链接。请先在游戏内打开一次「唤取记录」页面，再回来扫描");
        return -1;
    }
    if (game_dir && game_dir[0]) save_game_dir(root, dir); /* 记住本次使用的目录 */
    return 0;
}

/* ---------------- curl POST（JSON 体走临时文件，避免命令行转义问题） ---------------- */

static int curl_post_json(const char* url, const char* body, char** resp_out) {
    char tmpdir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpdir);
    char tmp_in[MAX_PATH];
    GetTempFileNameA(tmpdir, "gchi_", 0, tmp_in);
    FILE* f = fopen(tmp_in, "wb");
    if (!f) return -1;
    fwrite(body, 1, strlen(body), f);
    fclose(f);

    char tmp_out[MAX_PATH];
    GetTempFileNameA(tmpdir, "gcho_", 0, tmp_out);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    HANDLE hOut = CreateFileA(tmp_out, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOut;
    si.hStdError = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "\"C:\\Windows\\System32\\curl.exe\" -s --max-time 25 -X POST \"%s\" "
        "-H \"Content-Type: application/json\" "
        "-H \"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36\" "
        "-H \"Origin: https://aki-gm-resources.aki-game.com\" "
        "-H \"Referer: https://aki-gm-resources.aki-game.com/\" "
        "--data-binary \"@%s\"",
        url, tmp_in);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL created = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (si.hStdError && si.hStdError != INVALID_HANDLE_VALUE) CloseHandle(si.hStdError);
    CloseHandle(hOut);
    if (!created) { DeleteFileA(tmp_in); DeleteFileA(tmp_out); return -1; }
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    size_t len = 0;
    char* resp = gacha_read_file(tmp_out, &len);
    DeleteFileA(tmp_in);
    DeleteFileA(tmp_out);
    if (!resp || len < 2) { free(resp); return -1; }
    *resp_out = resp;
    return 0;
}

/* ---------------- 响应 JSON 解析（字符串感知的对象切片） ---------------- */

/* p 指向 '{'，返回其配对 '}' 的下一位置；防止名称中偶含大括号时错位 */
static const char* obj_span_end(const char* p, const char* limit) {
    int depth = 0;
    int in_str = 0, esc = 0;
    for (; p < limit; p++) {
        char c = *p;
        if (in_str) {
            if (esc) esc = 0;
            else if (c == '\\') esc = 1;
            else if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') in_str = 1;
        else if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) return p + 1; }
    }
    return NULL;
}

/* 动态字符串缓冲 */
typedef struct { char* buf; size_t len, cap; } StrBuf;

static int sb_append(StrBuf* sb, const char* data, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t nc = sb->cap ? sb->cap : (size_t)1 << 16;
        while (nc < sb->len + n + 1) nc *= 2;
        char* nb = (char*)realloc(sb->buf, nc);
        if (!nb) return -1;
        sb->buf = nb; sb->cap = nc;
    }
    memcpy(sb->buf + sb->len, data, n);
    sb->len += n;
    sb->buf[sb->len] = 0;
    return 0;
}

/* 容忍 "code":0 与 "code":"0" 两种写法 */
static int resp_code(const char* resp) {
    const char* p = strstr(resp, "\"code\"");
    if (!p) return -999;
    p += 6;
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p == '"') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') return -999;
    int v = atoi(p);
    return neg ? -v : v;
}

/* 拉取单个卡池：成功时把该卡池 data 数组内的对象原文拼接进 sb（逗号分隔），count 输出记录数 */
static int fetch_pool(const char* api_url, const GachaParams* p, const char* pool_id,
                      StrBuf* sb, int* count, char* err, int err_size) {
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"playerId\":\"%s\",\"recordId\":\"%s\",\"cardPoolId\":\"%s\",\"serverId\":\"%s\",\"languageCode\":\"%s\",\"cardPoolType\":\"%s\"}",
        p->player_id, p->record_id, p->resources_id, p->svr_id, p->lang, pool_id);

    char* resp = NULL;
    if (curl_post_json(api_url, body, &resp) != 0) {
        snprintf(err, err_size, "卡池%s请求失败（网络或curl错误）", pool_id);
        return -1;
    }

    int code = resp_code(resp);
    if (code != 0) {
        snprintf(err, err_size, "卡池%s：API错误码 %d（链接可能已过期，请在游戏内重新打开唤取记录页面后重试）", pool_id, code);
        free(resp);
        return -1;
    }

    const char* data_key = strstr(resp, "\"data\"");
    if (!data_key) { free(resp); *count = 0; return 0; }
    const char* lb = strchr(data_key, '[');
    if (!lb) { free(resp); *count = 0; return 0; }
    const char* limit = resp + strlen(resp);

    *count = 0;
    const char* cur = lb + 1;
    int ok = 1;
    while (cur < limit) {
        while (cur < limit && (*cur == ' ' || *cur == ',' || *cur == '\n' || *cur == '\r' || *cur == '\t')) cur++;
        if (cur >= limit || *cur != '{') break;
        const char* end = obj_span_end(cur, limit);
        if (!end) break;
        if (*count > 0 && sb_append(sb, ",", 1) != 0) { ok = 0; break; }
        if (sb_append(sb, cur, (size_t)(end - cur)) != 0) { ok = 0; break; }
        (*count)++;
        cur = end;
    }
    free(resp);
    if (!ok) snprintf(err, err_size, "卡池%s：内存不足", pool_id);
    return ok ? 0 : -1;
}

/* ---------------- 同步主流程（13 卡池并发，对齐上游速度） ---------------- */

typedef struct {
    const char* api_url;
    const GachaParams* gp;
    const char* pool_id;
    StrBuf* sb;
    int count;
    int rc;
    char perr[400];
} PoolJob;

static unsigned __stdcall pool_thread(void* arg) {
    PoolJob* j = (PoolJob*)arg;
    j->rc = fetch_pool(j->api_url, j->gp, j->pool_id, j->sb, &j->count, j->perr, sizeof(j->perr));
    return 0;
}

int mec_gacha_sync(const char* root, const char* url, char* out, int out_size) {
    GachaParams gp;
    char err[512] = { 0 };
    if (!url || !url[0]) {
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"抽卡链接不能为空\"}");
        return -1;
    }
    /* 校验并归一化链接 */
    {
        char norm[2048];
        snprintf(norm, sizeof(norm), "%s", url);
        mec_trim(norm);
        normalize_logged_url(norm);
        if (!is_gacha_record_url(norm)) {
            snprintf(out, out_size, "{\"ok\":false,\"error\":\"不是有效的唤取记录链接（应形如 %s#/record?...）\"}", GACHA_URL_PREFIX);
            return -1;
        }
        if (parse_gacha_params(norm, &gp, err, sizeof(err)) != 0) {
            snprintf(out, out_size, "{\"ok\":false,\"error\":\"%s\"}", err);
            return -1;
        }
    }

    const char* api_url = (gp.player_id[0] == '1') ? CN_API_URL : GLOBAL_API_URL;

    StrBuf pool_bufs[13];
    int pool_counts[13];
    memset(pool_bufs, 0, sizeof(pool_bufs));
    memset(pool_counts, 0, sizeof(pool_counts));

    char errors[2048] = { 0 };
    int err_pos = 0;
    int total = 0;
    int failed = 0;

    PoolJob jobs[13];
    HANDLE threads[13];
    memset(jobs, 0, sizeof(jobs));
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < 13; i++) {
        jobs[i].api_url = api_url;
        jobs[i].gp = &gp;
        jobs[i].pool_id = POOL_IDS[i];
        jobs[i].sb = &pool_bufs[i];
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, pool_thread, &jobs[i], 0, NULL);
        if (!threads[i]) { /* 起线程失败则当场同步执行兜底 */
            pool_thread(&jobs[i]);
        }
    }
    for (int i = 0; i < 13; i++) {
        if (threads[i]) {
            WaitForSingleObject(threads[i], 60000);
            CloseHandle(threads[i]);
        }
        if (jobs[i].rc == 0) {
            pool_counts[i] = jobs[i].count;
            total += jobs[i].count;
        } else {
            failed++;
            if (err_pos < (int)sizeof(errors) - 80)
                err_pos += snprintf(errors + err_pos, sizeof(errors) - err_pos, "%s%s", err_pos ? "；" : "", jobs[i].perr);
        }
    }

    /* 全部失败则不写盘 */
    if (total == 0 && failed > 0) {
        for (int i = 0; i < 13; i++) free(pool_bufs[i].buf);
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"%s\"}", errors[0] ? errors : "未获取到任何记录");
        return -1;
    }

    /* 合并写盘：先写临时文件再替换，失败不破坏旧数据 */
    char path[2200];
    snprintf(path, sizeof(path), "%s%cgacha_data.json", root ? root : ".", '\\');

    SYSTEMTIME st;
    GetLocalTime(&st);
    char sync_time[40];
    snprintf(sync_time, sizeof(sync_time), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    StrBuf all;
    memset(&all, 0, sizeof(all));
    int rc = 0;
    {
        char head[512];
        int hn = snprintf(head, sizeof(head),
            "{\"ok\":true,\"uid\":\"%s\",\"sync_time\":\"%s\",\"total\":%d,\"url_total\":%d,\"pools\":{",
            gp.player_id, sync_time, total, total);
        rc |= sb_append(&all, head, (size_t)hn);
    }
    for (int i = 0; i < 13; i++) {
        char k[24];
        int kn = snprintf(k, sizeof(k), "%s\"%s\":[", i ? "," : "", POOL_IDS[i]);
        rc |= sb_append(&all, k, (size_t)kn);
        if (pool_bufs[i].buf) rc |= sb_append(&all, pool_bufs[i].buf, pool_bufs[i].len);
        rc |= sb_append(&all, "]", 1);
    }
    rc |= sb_append(&all, "}}", 2);

    for (int i = 0; i < 13; i++) free(pool_bufs[i].buf);

    if (rc != 0 || !all.buf) {
        free(all.buf);
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"写入 gacha_data.json 失败（内存不足）\"}");
        return -1;
    }
    int wr = mec_write_file_utf8(path, all.buf, all.len);
    free(all.buf);
    if (wr != 0) {
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"写入 gacha_data.json 失败\"}");
        return -1;
    }

    snprintf(out, out_size,
        "{\"ok\":true,\"uid\":\"%s\",\"sync_time\":\"%s\",\"total\":%d,\"failed_pools\":%d,\"error\":\"%s\"}",
        gp.player_id, sync_time, total, failed, errors);
    return 0;
}
