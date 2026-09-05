#include "mec_cloud.h"
#include "http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* 云端代理。
   本机环境调用 WinHttpSendRequest 会崩溃（宿主注入钩子导致），故改用 Windows 自带
   curl.exe（Schannel TLS）作为 HTTPS 客户端：
     - payload 写入临时文件，curl --data-binary @file 提交
     - curl -D - 把响应头输出到 stdout，据此判定上游 HTTP 状态码
     - 状态码 <400：把 SSE body 流式透传给浏览器
     - 状态码 >=400 或无响应：发 data: {"error":...} 事件
*/

static char* extract_messages(const char* body) {
    if (!body) return NULL;
    const char* p = strstr(body, "\"messages\"");
    if (!p) return NULL;
    p = strchr(p, '[');
    if (!p) return NULL;
    int depth = 0, in_str = 0;
    const char* q = p;
    for (; *q; q++) {
        if (in_str) {
            if (*q == '\\') { q++; continue; }
            if (*q == '"') in_str = 0;
            continue;
        }
        if (*q == '"') { in_str = 1; continue; }
        if (*q == '[') depth++;
        else if (*q == ']') {
            depth--;
            if (depth == 0) break;
        }
    }
    if (*q != ']') return NULL;
    int len = (int)(q - p + 1);
    char* out = (char*)malloc((size_t)len + 1);
    memcpy(out, p, (size_t)len);
    out[len] = 0;
    return out;
}

static void send_sse_event(SOCKET c, const char* data_line) {
    char tmp[16384];
    snprintf(tmp, sizeof(tmp), "data: %s\n\n", data_line ? data_line : "");
    http_send_body(c, tmp, (unsigned long)strlen(tmp));
}

static int write_temp_file(const char* content, char* path_out, int path_size) {
    char tmpdir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmpdir);
    if (n == 0 || n >= MAX_PATH) snprintf(tmpdir, sizeof(tmpdir), ".");
    snprintf(path_out, path_size, "%smec_payload_%lu.json", tmpdir, GetCurrentProcessId());
    FILE* f = fopen(path_out, "wb");
    if (!f) return -1;
    size_t w = fwrite(content, 1, strlen(content), f);
    fclose(f);
    return w == strlen(content) ? 0 : -1;
}

int cloud_chat_stream(const MecCloudConfig* cfg, const char* request_json, SOCKET client) {
    char* messages = extract_messages(request_json);
    if (!messages) {
        http_send_error(client, 400, "missing messages");
        return -1;
    }

    /* 组装 messages 数组：若配置了 system_prompt 且前端没带 system，则作为首元素注入。
       注意 user_array 自带 []，注入时要跳过其左括号，避免把用户消息再套一层数组。 */
    char* user_array = messages;
    size_t ulen = strlen(user_array);
    int has_sys = cfg->system_prompt[0] && !strstr(user_array, "\"system\"");
    char* combined = (char*)malloc(ulen + sizeof(cfg->system_prompt) + 64);
    if (!combined) { free(messages); http_send_error(client, 500, "no memory"); return -1; }
    if (has_sys) {
        /* 转义 system_prompt 里的引号/反斜杠/换行 */
        char sys_esc[4096];
        int e = 0;
        for (const char* sp = cfg->system_prompt; *sp && e < (int)sizeof(sys_esc) - 4; sp++) {
            if (*sp == '"' || *sp == '\\') sys_esc[e++] = '\\';
            else if (*sp == '\n') { sys_esc[e++] = '\\'; sys_esc[e++] = 'n'; continue; }
            sys_esc[e++] = *sp;
        }
        sys_esc[e] = 0;
        /* combined = [ {system}, ...user(s) ] （跳过 user_array 的开头 [ ） */
        snprintf(combined, ulen + sizeof(sys_esc) + 64,
                 "[{\"role\":\"system\",\"content\":\"%s\"},%s", sys_esc, user_array + 1);
    } else {
        snprintf(combined, ulen + 1, "%s", user_array);
    }
    free(messages);

    char* payload = combined;
    size_t msg_len = strlen(payload);
    char* full_payload = (char*)malloc(msg_len + 1024);
    if (!full_payload) { free(payload); http_send_error(client, 500, "no memory"); return -1; }
    snprintf(full_payload, msg_len + 1024,
             "{\"model\":\"%s\",\"messages\":%s,\"stream\":true,"
             "\"max_tokens\":%d,\"temperature\":%.2f,\"top_p\":%.2f}",
             cfg->model, payload, cfg->max_tokens,
             (double)cfg->temperature, (double)cfg->top_p);
    free(payload);
    payload = full_payload;

    /* 完整 URL = base_url + "/chat/completions" */
    char url[1024];
    {
        char b[512];
        snprintf(b, sizeof(b), "%s", cfg->base_url);
        size_t bl = strlen(b);
        while (bl > 0 && (b[bl - 1] == '/' || b[bl - 1] == '\\')) b[--bl] = 0;
        snprintf(url, sizeof(url), "%s/chat/completions", b);
    }

    /* 写临时 payload 文件 */
    char tmp_path[MAX_PATH + 64];
    if (write_temp_file(payload, tmp_path, sizeof(tmp_path)) != 0) {
        free(payload);
        http_send_error(client, 500, "cannot write temp file");
        return -1;
    }

    /* 先发 SSE 头给浏览器 */
    http_send_status(client, 200, "OK");
    http_send_headers_stream(client, "text/event-stream; charset=utf-8", "Cache-Control: no-cache\r\n");

    /* 创建 curl 子进程：stdout 走管道，stderr 丢弃 */
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    HANDLE outR = NULL, outW = NULL;
    if (!CreatePipe(&outR, &outW, &sa, 0)) {
        send_sse_event(client, "{\"error\":\"cannot create pipe\"}");
        DeleteFileA(tmp_path);
        free(payload);
        return -1;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outW;
    si.hStdError = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "\"C:\\Windows\\System32\\curl.exe\" -s -N -X POST -D - \"%s\" "
             "-H \"Content-Type: application/json\" -H \"Authorization: Bearer %s\" "
             "--data-binary \"@%s\"",
             url, cfg->api_key[0] ? cfg->api_key : "", tmp_path);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL created = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (si.hStdError && si.hStdError != INVALID_HANDLE_VALUE) CloseHandle(si.hStdError);
    CloseHandle(outW);
    if (!created) {
        CloseHandle(outR);
        send_sse_event(client, "{\"error\":\"curl launch failed\"}");
        DeleteFileA(tmp_path);
        free(payload);
        return -1;
    }
    CloseHandle(pi.hThread);

    /* 读 curl 输出：先收响应头，解析状态码；<400 则流式转发 body */
    char* acc = (char*)malloc(65536);
    size_t used = 0;
    int hdr_done = 0;
    int status = 0;
    char buf[16384];
    DWORD got = 0;

    while (ReadFile(outR, buf, sizeof(buf), &got, NULL) && got > 0) {
        size_t pos = 0;
        if (!hdr_done) {
            size_t take = (got < (65536 - used - 1)) ? got : (65536 - used - 1);
            memcpy(acc + used, buf, take);
            used += take;
            acc[used] = 0;
            char* sep = strstr(acc, "\r\n\r\n");
            if (sep) {
                if (used >= 12 && strncmp(acc, "HTTP/", 5) == 0) status = atoi(acc + 9);
                size_t head_len = (size_t)(sep - acc) + 4;
                hdr_done = 1;
                if (status < 400) {
                    size_t body_in_acc = used - head_len;
                    if (body_in_acc) send(client, acc + head_len, (int)body_in_acc, 0);
                }
            } else if (used >= 65536 - 1) {
                hdr_done = 1;   /* 头异常，按 200 处理后续 */
            }
            pos = take;
            if (!hdr_done) continue;
        }
        if (status >= 400) break;
        if (got > pos) send(client, buf + pos, (int)(got - pos), 0);
    }
    free(acc);

    if (status >= 400) {
        char msg[160];
        snprintf(msg, sizeof(msg), "{\"error\":\"upstream HTTP %d\"}", status);
        send_sse_event(client, msg);
    } else if (status == 0) {
        send_sse_event(client, "{\"error\":\"无法连接云端 Agnes AI（curl 无响应）\"}");
    }

    /* 等待 curl 退出并清理 */
    WaitForSingleObject(pi.hProcess, 2000);
    CloseHandle(pi.hProcess);
    CloseHandle(outR);
    DeleteFileA(tmp_path);
    free(payload);
    return 0;
}
