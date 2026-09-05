/* mec_kuro.c - 库街区 API 代理（curl 子进程）+ 账号配置 */
#include "mec_kuro.h"
#include "mec_config.h"
#include "win_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static const char* kuro_base = "https://api.kurobbs.com";

int mec_kuro_load(const char* root, MecKuroConfig* cfg) {
    if (!cfg) return -1;
    memset(cfg, 0, sizeof(*cfg));
    char path[2048];
    snprintf(path, sizeof(path), "%s%ckuro_config.json", root ? root : ".",
             (root && root[0] && root[strlen(root) - 1] != '\\' && root[strlen(root) - 1] != '/') ? '\\' : '\0');
    FILE* f = mec_fopen_utf8(path, "rb");
    if (!f) return -1;
    char* buf = (char*)malloc(1 << 16);
    size_t n = fread(buf, 1, (1 << 16) - 1, f);
    fclose(f);
    buf[n] = 0;
    mec_json_get_string(buf, "token", cfg->token, (int)sizeof(cfg->token));
    mec_json_get_string(buf, "role_id", cfg->role_id, (int)sizeof(cfg->role_id));
    mec_json_get_string(buf, "server_id", cfg->server_id, (int)sizeof(cfg->server_id));
    free(buf);
    return 0;
}

int mec_kuro_save(const char* root, const MecKuroConfig* cfg) {
    if (!cfg) return -1;
    char path[2048];
    snprintf(path, sizeof(path), "%s%ckuro_config.json", root ? root : ".",
             (root && root[0] && root[strlen(root) - 1] != '\\' && root[strlen(root) - 1] != '/') ? '\\' : '\0');
    char out[2048];
    int n = snprintf(out, sizeof(out),
        "{\n  \"token\": \"%s\",\n  \"role_id\": \"%s\",\n  \"server_id\": \"%s\"\n}\n",
        cfg->token, cfg->role_id, cfg->server_id);
    FILE* f = mec_fopen_utf8(path, "wb");
    if (!f) return -1;
    fwrite(out, 1, (size_t)n, f);
    fclose(f);
    return 0;
}

int mec_kuro_post(const char* path, const char* token, const char* form_body, const char* extra_headers, char* out, int out_size) {
    if (!path || !out || out_size <= 0) return -1;

    /* 请求频率限制：连续请求间隔至少 1 秒，避免对库街区服务器造成压力 */
    static DWORD last_request_tick = 0;
    static int minute_count = 0;
    static DWORD minute_start = 0;
    DWORD now = GetTickCount();
    if (minute_start == 0 || now - minute_start > 60000) {
        minute_start = now;
        minute_count = 0;
    }
    if (last_request_tick > 0 && now - last_request_tick < 1000) {
        Sleep(1000 - (now - last_request_tick));
    }
    minute_count++;
    last_request_tick = GetTickCount();

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", kuro_base, path);

    /* token 头可选（登录/发验证码时没有 token） */
    char token_hdr[2048];
    if (token && token[0]) {
        snprintf(token_hdr, sizeof(token_hdr), "-H \"token: %s\" ", token);
    } else {
        token_hdr[0] = 0;
    }

    /* form-body 写临时文件，避免 cmd 转义问题 */
    char tmpdir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpdir);
    char tmp_in[MAX_PATH];
    GetTempFileNameA(tmpdir, "kri_", 0, tmp_in);
    FILE* f = fopen(tmp_in, "wb");
    if (!f) return -1;
    fwrite(form_body ? form_body : "", 1, form_body ? strlen(form_body) : 0, f);
    fclose(f);

    char tmp_out[MAX_PATH];
    GetTempFileNameA(tmpdir, "kro_", 0, tmp_out);

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

    char cmd[8192];
    if (extra_headers && extra_headers[0]) {
        snprintf(cmd, sizeof(cmd),
            "\"C:\\Windows\\System32\\curl.exe\" -s --max-time 25 -X POST \"%s\" "
            "%s%s "
            "-H \"source: ios\" "
            "-H \"Content-Type: application/x-www-form-urlencoded\" "
            "-H \"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36\" "
            "-H \"Origin: https://wiki.kurobbs.com\" "
            "-H \"Referer: https://wiki.kurobbs.com/\" "
            "--data-binary \"@%s\"",
            url, token_hdr, extra_headers, tmp_in);
    } else {
        snprintf(cmd, sizeof(cmd),
            "\"C:\\Windows\\System32\\curl.exe\" -s --max-time 25 -X POST \"%s\" "
            "%s"
            "-H \"source: ios\" "
            "-H \"Content-Type: application/x-www-form-urlencoded\" "
            "-H \"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36\" "
            "-H \"Origin: https://wiki.kurobbs.com\" "
            "-H \"Referer: https://wiki.kurobbs.com/\" "
            "--data-binary \"@%s\"",
            url, token_hdr, tmp_in);
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL created = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (si.hStdError && si.hStdError != INVALID_HANDLE_VALUE) CloseHandle(si.hStdError);
    CloseHandle(hOut);
    if (!created) { DeleteFileA(tmp_in); DeleteFileA(tmp_out); return -1; }
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    f = fopen(tmp_out, "rb");
    if (!f) { DeleteFileA(tmp_in); DeleteFileA(tmp_out); return -1; }
    size_t n = fread(out, 1, (size_t)(out_size - 1), f);
    out[n] = 0;
    fclose(f);
    DeleteFileA(tmp_in);
    DeleteFileA(tmp_out);
    return 0;
}
