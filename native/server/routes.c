#include "http_server.h"
#include "mec_config.h"
#include "mec_cloud.h"
#include "mec_kuro.h"
#include "mec_gacha.h"
#include "mec_common.h"
#include "win_utf8.h"
#include "echo_grade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ---------------- 小工具 ---------------- */

static const char* mime_type(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (_stricmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (_stricmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (_stricmp(dot, ".js") == 0) return "text/javascript; charset=utf-8";
    if (_stricmp(dot, ".mjs") == 0) return "text/javascript; charset=utf-8";
    if (_stricmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (_stricmp(dot, ".png") == 0) return "image/png";
    if (_stricmp(dot, ".jpg") == 0 || _stricmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(dot, ".webp") == 0) return "image/webp";
    if (_stricmp(dot, ".svg") == 0) return "image/svg+xml";
    if (_stricmp(dot, ".ico") == 0) return "image/x-icon";
    if (_stricmp(dot, ".gif") == 0) return "image/gif";
    if (_stricmp(dot, ".txt") == 0) return "text/plain; charset=utf-8";
    if (_stricmp(dot, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}

static int has_dotdot(const char* p) {
    return strstr(p, "..") != NULL || strchr(p, '\\') != NULL;
}

/* 把一个完整路径读到内存；成功返回 0 并写 buf/cap（宽字符打开，支持中文路径） */
static int read_file(const char* path, char** out, unsigned long* out_len) {
    FILE* f = mec_fopen_utf8(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *out = buf;
    *out_len = (unsigned long)got;
    return 0;
}

static void serve_static(const char* root, const char* url_path, SOCKET client) {
    if (has_dotdot(url_path)) {
        http_send_error(client, 400, "bad path");
        return;
    }
    char file_path[4096];
    /* 根路径默认跳转到 /account（游戏账户页） */
    if (strcmp(url_path, "/") == 0 || url_path[0] == 0) {
        http_send_status(client, 302, "Found");
        http_send_headers(client, "text/html; charset=utf-8", 0, "Location: /account\r\n");
        return;
    }
    /* /res/ -> resources/ ，/resources/ -> resources/ */
    if (strncmp(url_path, "/res/", 5) == 0) {
        snprintf(file_path, sizeof(file_path), "%s%cresources%s", root, MEC_SEP[0], url_path + 4);
    } else if (strncmp(url_path, "/resources/", 11) == 0) {
        snprintf(file_path, sizeof(file_path), "%s%c%s", root, MEC_SEP[0], url_path + 1);
    } else {
        if (strcmp(url_path, "/") == 0 || url_path[0] == 0) {
            snprintf(file_path, sizeof(file_path), "%s%cweb%cindex.html", root, MEC_SEP[0], MEC_SEP[0]);
        } else {
            snprintf(file_path, sizeof(file_path), "%s%cweb%s", root, MEC_SEP[0], url_path);
        }
    }
    /* 反斜杠统一 */
    for (char* p = file_path; *p; p++) if (*p == '/') *p = '\\';

    char* data = NULL;
    unsigned long len = 0;
    if (read_file(file_path, &data, &len) != 0) {
        /* SPA 路由回退：路径无扩展名且非API/资源路径时，返回 index.html 由前端路由处理 */
        int has_ext = 0;
        const char* basename = strrchr(url_path, '/');
        basename = basename ? basename + 1 : url_path;
        if (strchr(basename, '.')) has_ext = 1;
        if (!has_ext && strncmp(url_path, "/api/", 5) != 0 && strncmp(url_path, "/res/", 5) != 0 && strncmp(url_path, "/resources/", 11) != 0) {
            char idx_path[4096];
            snprintf(idx_path, sizeof(idx_path), "%s%cweb%cindex.html", root, MEC_SEP[0], MEC_SEP[0]);
            for (char* p = idx_path; *p; p++) if (*p == '/') *p = '\\';
            if (read_file(idx_path, &data, &len) == 0) {
                http_send_status(client, 200, "OK");
                http_send_headers(client, "text/html; charset=utf-8", len, NULL);
                http_send_body(client, data, len);
                free(data);
                return;
            }
        }
        http_send_error(client, 404, "not found");
        return;
    }
    http_send_status(client, 200, "OK");
    http_send_headers(client, mime_type(file_path), len, NULL);
    http_send_body(client, data, len);
    free(data);
}

/* ---------------- 数据库读取（输出 JSON） ---------------- */

static int build_characters_json(const char* root, char* out, int out_size) {
    char path[2048];
    snprintf(path, sizeof(path), "%s%cdatabase%ccharacters.bin", root, MEC_SEP[0], MEC_SEP[0]);
    char* txt = NULL; unsigned long n = 0;
    if (read_file(path, &txt, &n) != 0) {
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"characters.bin missing\"}");
        return -1;
    }
    char* names[512];
    int count = 0;
    char* save = NULL;
    char* line = strtok_r(txt, "\r\n", &save);
    while (line && count < 512) {
        if (strncmp(line, "CHAR|", 5) == 0) {
            char* p = line + 5;
            char* sep = strchr(p, '|');
            if (sep) { *sep = 0; }
            mec_trim(p);
            if (p[0]) names[count++] = p;
        }
        line = strtok_r(NULL, "\r\n", &save);
    }
    /* 排序（UTF-8 字节序近似码点序） */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (strcmp(names[j], names[i]) < 0) { char* t = names[i]; names[i] = names[j]; names[j] = t; }

    int pos = 0;
    mec_json_append(out, out_size, &pos, "{\"ok\":true,\"characters\":[");
    for (int i = 0; i < count; i++) {
        /* 对名字做 JSON 转义（双引号/反斜杠） */
        char esc[256]; int e = 0;
        for (char* cp = names[i]; *cp && e < 250; cp++) {
            if (*cp == '"' || *cp == '\\') esc[e++] = '\\';
            esc[e++] = *cp;
        }
        esc[e] = 0;
        mec_json_append(out, out_size, &pos, "%s\"%s\"", i ? "," : "", esc);
    }
    mec_json_append(out, out_size, &pos, "]}");
    free(txt);
    return 0;
}

static void append_csv_lines(const char* root, const char* fname, const char* prefix,
                             char* out, int out_size, int* pos, const char* obj_fmt) {
    char path[2048];
    snprintf(path, sizeof(path), "%s%cdatabase%c%s", root, MEC_SEP[0], MEC_SEP[0], fname);
    char* txt = NULL; unsigned long n = 0;
    if (read_file(path, &txt, &n) != 0) return;
    char* save = NULL;
    char* line = strtok_r(txt, "\r\n", &save);
    size_t plen = strlen(prefix);
    int first = 1;
    while (line) {
        if (strncmp(line, prefix, plen) == 0) {
            mec_json_append(out, out_size, pos, "%s", first ? "" : ",");
            /* obj_fmt 包含 %s 占位，需按字段拆 */
            char* fields[16];
            int fc = 0;
            char* save2 = NULL;
            char* tok = strtok_r(line + (int)plen, "|", &save2);
            while (tok && fc < 16) { fields[fc++] = tok; tok = strtok_r(NULL, "|", &save2); }
            /* 按 obj_fmt 中的 N 个 %s 填字段 */
            const char* f = obj_fmt;
            while (*f) {
                if (*f == '%' && f[1] == 's') {
                    int idx = atoi(f + 2);
                    /* 支持 %s%d 形式的字段索引 */
                    const char* q = f + 2;
                    while (*q >= '0' && *q <= '9') q++;
                    if (idx == 0) { /* 未指定则按出现顺序 */ }
                    char* val = (idx >= 1 && idx <= fc) ? fields[idx - 1] : "";
                    /* 转义 */
                    char esc[512]; int e = 0;
                    for (char* cp = val; *cp && e < 500; cp++) {
                        if (*cp == '"' || *cp == '\\') esc[e++] = '\\';
                        esc[e++] = *cp;
                    }
                    esc[e] = 0;
                    mec_json_append(out, out_size, pos, "\"%s\"", esc);
                    f = q;
                    continue;
                } else {
                    mec_json_append(out, out_size, pos, "%c", *f);
                }
                f++;
            }
            first = 0;
        }
        line = strtok_r(NULL, "\r\n", &save);
    }
    free(txt);
}

static int build_database_json(const char* root, char* out, int out_size) {
    int pos = 0;
    mec_json_append(out, out_size, &pos,
        "{\"ok\":true,\"counts\":{\"characters\":%d,\"echoes\":%d,\"weapons\":%d,\"resonance\":%d,\"resonance_echoes\":%d},"
        "\"echoes\":[",
        mec_db_count_characters(), mec_db_count_echoes(), mec_db_count_weapons(), mec_db_count_resonance(),
        mec_db_count_resonance_echoes());
    append_csv_lines(root, "echoes.bin", "ECHO|", out, out_size, &pos, "{\"name\":%s1,\"cost\":%s2,\"level\":%s3,\"image\":%s4}");
    mec_json_append(out, out_size, &pos, "],\"weapons\":[");
    append_csv_lines(root, "weapons.bin", "WEAPON|", out, out_size, &pos, "{\"name\":%s1}");
    mec_json_append(out, out_size, &pos, "],\"resonance\":[");
    append_csv_lines(root, "resonance.bin", "RESONANCE|", out, out_size, &pos, "{\"name\":%s1}");
    mec_json_append(out, out_size, &pos, "],\"resonance_echoes\":[");
    append_csv_lines(root, "resonance_echoes.bin", "SET|", out, out_size, &pos, "{\"set\":%s1,\"cost\":%s2,\"echo\":%s3}");
    mec_json_append(out, out_size, &pos, "]}");
    return 0;
}

/* ---------------- 路由 ---------------- */

void http_route(const char* root, HttpRequest* req, SOCKET client) {
    /* 路径解码（%XX） */
    char decoded[2048];
    const char* src = req->path;
    int d = 0;
    for (int i = 0; src[i] && d < (int)sizeof(decoded) - 1; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char tmp[3] = { src[i + 1], src[i + 2], 0 };
            decoded[d++] = (char)strtol(tmp, NULL, 16);
            i += 2;
        } else {
            decoded[d++] = src[i];
        }
    }
    decoded[d] = 0;
    const char* path = decoded;

    /* ---- API ---- */
    if (strcmp(path, "/api/ping") == 0) {
        http_send_json(client, 200, "{\"ok\":true,\"name\":\"MingEchoServer\",\"version\":\"1.2.0\"}");
        return;
    }
    if (strcmp(path, "/api/status") == 0) {
        MecCloudConfig cfg;
        mec_config_load(root, &cfg);
        char out[8192];
        snprintf(out, sizeof(out),
            "{\"ok\":true,\"version\":\"%s\",\"db_loaded\":%s,"
            "\"counts\":{\"characters\":%d,\"echoes\":%d,\"weapons\":%d,\"resonance\":%d,\"resonance_echoes\":%d},"
            "\"cloud\":{\"model\":\"%s\",\"base_url\":\"%s\",\"has_key\":%s,\"max_tokens\":%d}}",
            mec_version(), mec_db_count_characters() > 0 ? "true" : "false",
            mec_db_count_characters(), mec_db_count_echoes(), mec_db_count_weapons(), mec_db_count_resonance(), mec_db_count_resonance_echoes(),
            cfg.model, cfg.base_url, cfg.api_key[0] ? "true" : "false", cfg.max_tokens);
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/characters") == 0) {
        char out[16384];
        build_characters_json(root, out, sizeof(out));
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/database") == 0) {
        char out[262144];
        build_database_json(root, out, sizeof(out));
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/parse") == 0 && strcmp(req->method, "POST") == 0) {
        char text[MEC_MAX_TEXT] = { 0 };
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        mec_json_get_string(req->body, "text", text, sizeof(text));
        char out[MEC_MAX_JSON * 4];
        int rc = mec_parse_stats_json(text, out, sizeof(out));
        if (rc != 0 && out[0] != '{') snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", mec_last_error());
        http_send_json(client, rc == 0 ? 200 : 400, out);
        return;
    }
    if (strcmp(path, "/api/echo_grade") == 0 && strcmp(req->method, "POST") == 0) {
        char character[128] = { 0 }, grade_data[16384] = { 0 };
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        mec_json_get_string(req->body, "character", character, sizeof(character));
        mec_json_get_string(req->body, "data", grade_data, sizeof(grade_data));
        char out[MEC_MAX_JSON];
        int rc = mec_echo_grade_json(root, character, grade_data, out, sizeof(out));
        if (rc != 0 && out[0] != '{') snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", mec_last_error());
        http_send_json(client, rc == 0 ? 200 : 400, out);
        return;
    }
    if (strcmp(path, "/api/score") == 0 && strcmp(req->method, "POST") == 0) {
        char character[128] = { 0 }, echo_text[MEC_MAX_TEXT] = { 0 }, panel_text[MEC_MAX_TEXT] = { 0 };
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        mec_json_get_string(req->body, "character", character, sizeof(character));
        mec_json_get_string(req->body, "echo_text", echo_text, sizeof(echo_text));
        mec_json_get_string(req->body, "panel_text", panel_text, sizeof(panel_text));
        char out[MEC_MAX_JSON * 4];
        int rc = mec_score_echo_json(character, echo_text, panel_text, out, sizeof(out));
        if (rc != 0 && out[0] != '{') snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", mec_last_error());
        http_send_json(client, rc == 0 ? 200 : 400, out);
        return;
    }
    if (strcmp(path, "/api/recommend") == 0 && strcmp(req->method, "POST") == 0) {
        char character[128] = { 0 }, panel_text[MEC_MAX_TEXT] = { 0 };
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        mec_json_get_string(req->body, "character", character, sizeof(character));
        mec_json_get_string(req->body, "panel_text", panel_text, sizeof(panel_text));
        char out[MEC_MAX_JSON * 4];
        int rc = mec_recommend_json(character, panel_text, out, sizeof(out));
        if (rc != 0 && out[0] != '{') snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", mec_last_error());
        http_send_json(client, rc == 0 ? 200 : 400, out);
        return;
    }
    if (strcmp(path, "/api/chat") == 0 && strcmp(req->method, "POST") == 0) {
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        MecCloudConfig cfg;
        mec_config_load(root, &cfg);
        if (!cfg.api_key[0]) {
            http_send_error(client, 400, "cloud.api_key 未配置，请在 config.json 填写 Agnes AI 密钥");
            return;
        }
        cloud_chat_stream(&cfg, req->body, client);
        return;
    }
    if (strcmp(path, "/api/targets") == 0) {
        /* 目标面板数据（database/character_targets.json，热读：编辑后刷新即生效） */
        char file_path[4096];
        snprintf(file_path, sizeof(file_path), "%s%cdatabase%ccharacter_targets.json", root, MEC_SEP[0], MEC_SEP[0]);
        for (char* p = file_path; *p; p++) if (*p == '/') *p = '\\';
        char* data = NULL; unsigned long len = 0;
        if (read_file(file_path, &data, &len) != 0) {
            http_send_json(client, 200, "{\"ok\":false,\"error\":\"character_targets.json 不存在\"}");
            return;
        }
        /* 去 BOM */
        if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
            memmove(data, data + 3, len - 3);
            data[len - 3] = 0;
            len -= 3;
        }
        http_send_status(client, 200, "OK");
        http_send_headers(client, "application/json; charset=utf-8", len, NULL);
        http_send_body(client, data, len);
        free(data);
        return;
    }

    if (strncmp(path, "/api/role_panel", 15) == 0) {
        /* 角色目标面板（从 resources/Role/角色面板.md 读取指定角色段落） */
        char name[128] = { 0 };
        if (req->query && req->query[0]) {
            const char* p = strstr(req->query, "name=");
            if (p) {
                p += 5;
                int i = 0;
                while (*p && *p != '&' && i < (int)sizeof(name) - 1) {
                    if (*p == '%' && p[1] && p[2]) {
                        char hex[3] = { p[1], p[2], 0 };
                        name[i++] = (char)strtol(hex, NULL, 16);
                        p += 3;
                    } else {
                        name[i++] = (*p == '+') ? ' ' : *p;
                        p++;
                    }
                }
                name[i] = 0;
            }
        }
        char md_path[4096];
        snprintf(md_path, sizeof(md_path), "%s%cresources%cRole%c角色面板.md", root, MEC_SEP[0], MEC_SEP[0], MEC_SEP[0]);
        for (char* p = md_path; *p; p++) if (*p == '/') *p = '\\';
        char* md = NULL; unsigned long md_len = 0;
        if (read_file(md_path, &md, &md_len) != 0) {
            http_send_json(client, 200, "{\"ok\":false,\"error\":\"角色面板.md 不存在\"}");
            return;
        }
        /* 去 BOM */
        if (md_len >= 3 && (unsigned char)md[0] == 0xEF && (unsigned char)md[1] == 0xBB && (unsigned char)md[2] == 0xBF) {
            memmove(md, md + 3, md_len - 3);
            md[md_len - 3] = 0;
            md_len -= 3;
        }
        /* 查找 "# 角色名" 段落 */
        char search[160];
        snprintf(search, sizeof(search), "# %s", name);
        char* start = strstr(md, search);
        char* section = NULL;
        if (start) {
            /* 找到下一个 "# " 作为结束 */
            char* end = strstr(start + strlen(search), "\n# ");
            if (!end) end = md + md_len;
            size_t sec_len = (size_t)(end - start);
            section = (char*)malloc(sec_len + 1);
            if (section) {
                memcpy(section, start, sec_len);
                section[sec_len] = 0;
            }
        }
        /* JSON 转义 */
        char* escaped = NULL;
        if (section) {
            size_t elen = strlen(section) * 2 + 16;
            escaped = (char*)malloc(elen);
            if (escaped) {
                int j = 0;
                for (size_t i = 0; section[i]; i++) {
                    if (section[i] == '"') { escaped[j++] = '\\'; escaped[j++] = '"'; }
                    else if (section[i] == '\\') { escaped[j++] = '\\'; escaped[j++] = '\\'; }
                    else if (section[i] == '\n') { escaped[j++] = '\\'; escaped[j++] = 'n'; }
                    else if (section[i] == '\r') { /* skip */ }
                    else escaped[j++] = section[i];
                }
                escaped[j] = 0;
            }
        }
        char out[8192];
        if (section && escaped) {
            snprintf(out, sizeof(out), "{\"ok\":true,\"name\":\"%s\",\"panel\":\"%s\"}", name, escaped);
        } else {
            snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"未找到角色 %s 的目标面板\"}", name);
        }
        http_send_json(client, 200, out);
        free(md);
        if (section) free(section);
        if (escaped) free(escaped);
        return;
    }

    if (strcmp(path, "/api/config/api_key") == 0 && strcmp(req->method, "POST") == 0) {
        /* 更新 config.json 里的 api_key（旧密钥被覆盖删除，下次请求自动生效） */
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        char new_key[512] = { 0 };
        mec_json_get_string(req->body, "api_key", new_key, sizeof(new_key));
        if (!new_key[0]) { http_send_json(client, 400, "{\"ok\":false,\"error\":\"api_key 不能为空\"}"); return; }
        int rc = mec_config_set_api_key(root, new_key);
        if (rc != 0) { http_send_json(client, 500, "{\"ok\":false,\"error\":\"写入 config.json 失败\"}"); return; }
        http_send_json(client, 200, "{\"ok\":true,\"message\":\"API 秘钥已更新，旧密钥已覆盖删除\"}");
        return;
    }

    /* ---- 库街区游戏账户 ---- */
    if (strcmp(path, "/api/kuro/config") == 0 && strcmp(req->method, "GET") == 0) {
        MecKuroConfig kc;
        int rc = mec_kuro_load(root, &kc);
        char out[1024];
        if (rc != 0 || !kc.token[0]) {
            snprintf(out, sizeof(out), "{\"ok\":true,\"configured\":false,\"token\":\"\",\"role_id\":\"\",\"server_id\":\"\"}");
        } else {
            /* token 脱敏：只显示前 12 位 */
            char masked[64];
            int tlen = (int)strlen(kc.token);
            if (tlen > 12) snprintf(masked, sizeof(masked), "%.*s…", 12, kc.token);
            else snprintf(masked, sizeof(masked), "%s", kc.token);
            snprintf(out, sizeof(out),
                "{\"ok\":true,\"configured\":true,\"token\":\"%s\",\"role_id\":\"%s\",\"server_id\":\"%s\"}",
                masked, kc.role_id, kc.server_id);
        }
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/kuro/config") == 0 && strcmp(req->method, "POST") == 0) {
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        MecKuroConfig kc;
        memset(&kc, 0, sizeof(kc));
        mec_json_get_string(req->body, "token", kc.token, sizeof(kc.token));
        mec_json_get_string(req->body, "role_id", kc.role_id, sizeof(kc.role_id));
        mec_json_get_string(req->body, "server_id", kc.server_id, sizeof(kc.server_id));
        if (!kc.token[0]) { http_send_json(client, 400, "{\"ok\":false,\"error\":\"token 不能为空\"}"); return; }
        int rc = mec_kuro_save(root, &kc);
        if (rc != 0) { http_send_json(client, 500, "{\"ok\":false,\"error\":\"保存失败\"}"); return; }
        http_send_json(client, 200, "{\"ok\":true,\"message\":\"库街区账号已保存\"}");
        return;
    }
    /* 清除库街区配置（删除 kuro_config.json） */
    if (strcmp(path, "/api/kuro/config/clear") == 0 && strcmp(req->method, "POST") == 0) {
        char path2[2048];
        snprintf(path2, sizeof(path2), "%s%ckuro_config.json", root ? root : ".",
                 (root && root[0] && root[strlen(root) - 1] != '\\' && root[strlen(root) - 1] != '/') ? '\\' : '\0');
        DeleteFileA(path2);
        http_send_json(client, 200, "{\"ok\":true,\"message\":\"库街区配置已清除\"}");
        return;
    }
    /* ---- 库街区短信登录：发送验证码 ---- */
    if (strcmp(path, "/api/kuro/send_code") == 0 && strcmp(req->method, "POST") == 0) {
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        char mobile[32] = {0}, geetest[4096] = {0}, dev_code[128] = {0};
        mec_json_get_string(req->body, "mobile", mobile, sizeof(mobile));
        mec_json_get_string(req->body, "geetest_data", geetest, sizeof(geetest));
        mec_json_get_string(req->body, "dev_code", dev_code, sizeof(dev_code));
        if (!mobile[0] || !geetest[0]) {
            http_send_json(client, 400, "{\"ok\":false,\"error\":\"mobile 和 geetest_data 不能为空\"}");
            return;
        }
        /* URL-encode geetest_data */
        char enc[8192]; int ei = 0;
        for (int i = 0; geetest[i] && ei < (int)sizeof(enc)-4; i++) {
            unsigned char c = (unsigned char)geetest[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                enc[ei++] = c;
            } else {
                ei += snprintf(enc + ei, sizeof(enc) - ei, "%%%02X", c);
            }
        }
        enc[ei] = 0;
        char form[8192];
        snprintf(form, sizeof(form), "geetest_data=%s&mobile=%s", enc, mobile);
        char extra[1024];
        if (dev_code[0]) snprintf(extra, sizeof(extra), "-H \"devCode: %s\"", dev_code);
        else extra[0] = 0;
        char* resp = (char*)malloc(1 << 20);
        int rc = mec_kuro_post("/user/getSmsCode", NULL, form, extra[0] ? extra : NULL, resp, 1 << 20);
        if (rc != 0) { free(resp); http_send_json(client, 502, "{\"ok\":false,\"error\":\"发送验证码失败\"}"); return; }
        http_send_status(client, 200, "OK");
        http_send_headers(client, "application/json; charset=utf-8", (unsigned long)strlen(resp), NULL);
        http_send_body(client, resp, (unsigned long)strlen(resp));
        free(resp);
        return;
    }
    /* ---- 库街区短信登录：登录获取 token ---- */
    if (strcmp(path, "/api/kuro/sdk_login") == 0 && strcmp(req->method, "POST") == 0) {
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        char mobile[32] = {0}, code[16] = {0}, dev_code[128] = {0};
        mec_json_get_string(req->body, "mobile", mobile, sizeof(mobile));
        mec_json_get_string(req->body, "code", code, sizeof(code));
        mec_json_get_string(req->body, "dev_code", dev_code, sizeof(dev_code));
        if (!mobile[0] || !code[0]) {
            http_send_json(client, 400, "{\"ok\":false,\"error\":\"mobile 和 code 不能为空\"}");
            return;
        }
        char form[256];
        snprintf(form, sizeof(form), "mobile=%s&code=%s", mobile, code);
        char extra[1024];
        if (dev_code[0]) snprintf(extra, sizeof(extra), "-H \"devCode: %s\"", dev_code);
        else extra[0] = 0;
        char* resp = (char*)malloc(1 << 20);
        int rc = mec_kuro_post("/user/sdkLogin", NULL, form, extra[0] ? extra : NULL, resp, 1 << 20);
        if (rc != 0) { free(resp); http_send_json(client, 502, "{\"ok\":false,\"error\":\"登录失败\"}"); return; }
        http_send_status(client, 200, "OK");
        http_send_headers(client, "application/json; charset=utf-8", (unsigned long)strlen(resp), NULL);
        http_send_body(client, resp, (unsigned long)strlen(resp));
        free(resp);
        return;
    }
    if (strcmp(path, "/api/kuro/proxy") == 0 && strcmp(req->method, "POST") == 0) {
        /* 通用代理：把请求转发到库街区 API，原样返回 JSON */
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        char kpath[512] = { 0 }, form[4096] = { 0 }, headers_obj[2048] = { 0 };
        mec_json_get_string(req->body, "path", kpath, sizeof(kpath));
        mec_json_get_string(req->body, "form", form, sizeof(form));
        mec_json_get_string(req->body, "headers", headers_obj, sizeof(headers_obj));
        if (!kpath[0]) { http_send_json(client, 400, "{\"ok\":false,\"error\":\"path 不能为空\"}"); return; }
        MecKuroConfig kc;
        if (mec_kuro_load(root, &kc) != 0 || !kc.token[0]) {
            http_send_json(client, 400, "{\"ok\":false,\"error\":\"未配置库街区 token，请先在游戏账户页填写\"}");
            return;
        }
        /* 从 headers 对象中提取 b-at，构建附加请求头 */
        char extra_headers[1024] = { 0 };
        if (headers_obj[0]) {
            char bat[512] = { 0 };
            mec_json_get_string(headers_obj, "b-at", bat, sizeof(bat));
            if (bat[0]) {
                snprintf(extra_headers, sizeof(extra_headers), "-H \"b-at: %s\"", bat);
            }
        }
        char* resp = (char*)malloc(1 << 20);
        /* 有 b-at 时不发 token 头（同时发会导致库街区 API 返回 10000 参数错误） */
        int rc = mec_kuro_post(kpath, extra_headers[0] ? NULL : kc.token, form[0] ? form : NULL, extra_headers[0] ? extra_headers : NULL, resp, 1 << 20);
        if (rc != 0) {
            free(resp);
            http_send_json(client, 502, "{\"ok\":false,\"error\":\"库街区 API 请求失败（curl 错误）\"}");
            return;
        }
        /* 原样返回库街区的 JSON 响应 */
        http_send_status(client, 200, "OK");
        http_send_headers(client, "application/json; charset=utf-8", (unsigned long)strlen(resp), NULL);
        http_send_body(client, resp, (unsigned long)strlen(resp));
        free(resp);
        return;
    }

    /* ---- 抽卡记录（参考 juliy819/wuwa-gacha-tool，纯 C 实现） ---- */
    if (strcmp(path, "/api/gacha/config") == 0 && strcmp(req->method, "GET") == 0) {
        /* 返回上次保存的游戏目录与最近同步时间 */
        char dir[2048] = { 0 };
        char cfg_path[2200];
        snprintf(cfg_path, sizeof(cfg_path), "%s%cgacha_config.json", root ? root : ".", '\\');
        char* cfg = NULL; unsigned long cfg_len = 0;
        if (read_file(cfg_path, &cfg, &cfg_len) == 0) {
            mec_json_get_string(cfg, "game_dir", dir, sizeof(dir));
            free(cfg);
            /* 还原 JSON 转义的双反斜杠 */
            char* r = dir; char* w = dir;
            while (*r) { if (r[0] == '\\' && r[1] == '\\') r++; *w++ = *r++; }
            *w = 0;
        }
        char esc[4200]; int e = 0;
        for (char* c = dir; *c && e < (int)sizeof(esc) - 2; c++) {
            if (*c == '\\') esc[e++] = '\\';
            esc[e++] = *c;
        }
        esc[e] = 0;
        char out[4600];
        snprintf(out, sizeof(out), "{\"ok\":true,\"game_dir\":\"%s\"}", esc);
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/gacha/scan") == 0 && strcmp(req->method, "POST") == 0) {
        /* 扫描 Client.log 提取唤取记录链接（game_dir 可为空，用上次保存的目录） */
        char game_dir[2048] = { 0 };
        if (req->body) mec_json_get_string(req->body, "game_dir", game_dir, sizeof(game_dir));
        char url[2048] = { 0 }, err[512] = { 0 };
        int rc = mec_gacha_scan(root, game_dir[0] ? game_dir : NULL, url, sizeof(url), err, sizeof(err));
        if (rc != 0) {
            char out[1024];
            snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", err);
            http_send_json(client, 200, out);
            return;
        }
        /* 链接仅本机传输；前端拿原串回传 /api/gacha/sync */
        char esc[4200]; int e = 0;
        for (char* c = url; *c && e < (int)sizeof(esc) - 2; c++) {
            if (*c == '"' || *c == '\\') esc[e++] = '\\';
            esc[e++] = *c;
        }
        esc[e] = 0;
        char out[4300];
        snprintf(out, sizeof(out), "{\"ok\":true,\"url\":\"%s\"}", esc);
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/gacha/sync") == 0 && strcmp(req->method, "POST") == 0) {
        /* 用唤取记录链接同步官方接口，13 卡池全量写入 gacha_data.json */
        if (!req->body) { http_send_error(client, 400, "no body"); return; }
        char url[2048] = { 0 };
        mec_json_get_string(req->body, "url", url, sizeof(url));
        char out[4096];
        mec_gacha_sync(root, url, out, sizeof(out));
        http_send_json(client, 200, out);
        return;
    }
    if (strcmp(path, "/api/gacha/records") == 0 && strcmp(req->method, "GET") == 0) {
        /* 返回已同步的全部抽卡记录（gacha_data.json 原样，可能数 MB） */
        char file_path[2200];
        snprintf(file_path, sizeof(file_path), "%s%cgacha_data.json", root ? root : ".", '\\');
        char* data = NULL; unsigned long len = 0;
        if (read_file(file_path, &data, &len) != 0) {
            http_send_json(client, 200, "{\"ok\":false,\"error\":\"尚未同步抽卡记录\",\"total\":0,\"pools\":{}}");
            return;
        }
        http_send_status(client, 200, "OK");
        http_send_headers(client, "application/json; charset=utf-8", len, NULL);
        http_send_body(client, data, len);
        free(data);
        return;
    }

    /* ---- 静态文件 ---- */
    serve_static(root, path, client);
}
