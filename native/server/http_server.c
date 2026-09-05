#include "http_server.h"
#include "mec_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HEADER_BYTES (64 * 1024)

/* ---------------- 发送工具 ---------------- */

static int send_all(SOCKET c, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(c, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

void http_send_status(SOCKET c, int code, const char* text) {
    char h[128];
    snprintf(h, sizeof(h), "HTTP/1.1 %d %s\r\n", code, text ? text : "OK");
    send_all(c, h, (int)strlen(h));
}

void http_send_headers(SOCKET c, const char* content_type, unsigned long length, const char* extra) {
    char h[1024];
    int n = 0;
    n += snprintf(h + n, sizeof(h) - n, "Content-Type: %s\r\n", content_type ? content_type : "application/octet-stream");
    n += snprintf(h + n, sizeof(h) - n, "Content-Length: %lu\r\n", length);
    n += snprintf(h + n, sizeof(h) - n, "Cache-Control: no-store\r\n");
    n += snprintf(h + n, sizeof(h) - n, "Access-Control-Allow-Origin: *\r\n");
    if (extra) n += snprintf(h + n, sizeof(h) - n, "%s", extra);
    n += snprintf(h + n, sizeof(h) - n, "Connection: close\r\n\r\n");
    send_all(c, h, n);
}

void http_send_headers_stream(SOCKET c, const char* content_type, const char* extra) {
    char h[1024];
    int n = 0;
    n += snprintf(h + n, sizeof(h) - n, "Content-Type: %s\r\n", content_type ? content_type : "text/event-stream; charset=utf-8");
    n += snprintf(h + n, sizeof(h) - n, "Cache-Control: no-cache\r\n");
    n += snprintf(h + n, sizeof(h) - n, "Access-Control-Allow-Origin: *\r\n");
    if (extra) n += snprintf(h + n, sizeof(h) - n, "%s", extra);
    n += snprintf(h + n, sizeof(h) - n, "Connection: close\r\n\r\n");
    send_all(c, h, n);
}

void http_send_body(SOCKET c, const char* data, unsigned long len) {
    send_all(c, data, (int)len);
}

void http_send_json(SOCKET c, int code, const char* text) {
    http_send_status(c, code, code == 200 ? "OK" : "Error");
    http_send_headers(c, "application/json; charset=utf-8", (unsigned long)strlen(text), NULL);
    http_send_body(c, text, (unsigned long)strlen(text));
}

void http_send_error(SOCKET c, int code, const char* msg) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg ? msg : "error");
    http_send_json(c, code, buf);
}

/* ---------------- 请求解析 ---------------- */

static int recv_until_headers(SOCKET c, char* buf, int max, int* out_len) {
    int total = 0;
    while (total < max) {
        int n = recv(c, buf + total, max - total, 0);
        if (n <= 0) {
            if (n == 0) break;               /* 对端关闭 */
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) break;
            return -1;
        }
        total += n;
        buf[total] = 0;
        if (total >= 4 && strstr(buf, "\r\n\r\n")) break;
    }
    *out_len = total;
    return total > 0 ? 0 : -1;
}

static void parse_request_line(char* line, HttpRequest* req) {
    char* p = line;
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(req->method) - 1) req->method[i++] = *p++;
    req->method[i] = 0;
    while (*p == ' ' || *p == '\t') p++;
    /* path[?query] */
    i = 0;
    char* q = NULL;
    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(req->path) - 1) {
        if (*p == '?') { q = p + 1; break; }
        req->path[i++] = *p++;
    }
    req->path[i] = 0;
    if (q) {
        i = 0;
        while (*q && *q != ' ' && *q != '\t' && i < (int)sizeof(req->query) - 1) req->query[i++] = *q++;
        req->query[i] = 0;
    }
}

/* ---------------- 连接处理（线程） ---------------- */

typedef struct ClientJob {
    SOCKET client;
    const char* root;
} ClientJob;

static DWORD WINAPI handle_client(LPVOID param) {
    ClientJob* job = (ClientJob*)param;
    SOCKET c = job->client;
    const char* root = job->root;
    free(job);

    /* 连接超时，避免死连接挂线程 */
    int to = 30000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));

    HttpRequest req;
    memset(&req, 0, sizeof(req));
    req.client = c;
    req.body = NULL;

    char* buf = (char*)malloc(MAX_HEADER_BYTES + 1);
    int hlen = 0;
    if (recv_until_headers(c, buf, MAX_HEADER_BYTES, &hlen) == 0 && hlen > 0) {
        buf[hlen] = 0;
        /* 先定位 body 起点（在解析头部前），避免 strtok 破坏缓冲区 */
        char* body_start = strstr(buf, "\r\n\r\n");
        /* 请求行 */
        char* header_end = strstr(buf, "\r\n");
        if (header_end) {
            *header_end = 0;
            parse_request_line(buf, &req);
            /* 头字段（在副本上解析，避免污染原缓冲区） */
            char* rest = header_end + 2;
            unsigned long clen = 0;
            if (body_start) {
                char* hcopy = (char*)malloc((size_t)(body_start - rest) + 1);
                if (hcopy) {
                    size_t hlen2 = (size_t)(body_start - rest);
                    memcpy(hcopy, rest, hlen2);
                    hcopy[hlen2] = 0;
                    char* save = NULL;
                    char* line = strtok_r(hcopy, "\r\n", &save);
                    while (line) {
                        if (_strnicmp(line, "Content-Length:", 15) == 0) {
                            clen = strtoul(line + 15, NULL, 10);
                        }
                        line = strtok_r(NULL, "\r\n", &save);
                    }
                    free(hcopy);
                }
            }
            req.content_length = clen;
            /* body */
            if (clen > 0 && clen < 64 * 1024 * 1024 && body_start) {
                req.body = (char*)malloc(clen + 1);
                long copied = 0;
                const char* bs = body_start + 4;
                long avail = hlen - (long)(bs - buf);
                if (avail > 0) {
                    int cp = avail < (long)clen ? avail : (long)clen;
                    memcpy(req.body, bs, cp);
                    copied = cp;
                }
                while (copied < (long)clen) {
                    int n = recv(c, req.body + copied, (int)(clen - copied), 0);
                    if (n <= 0) break;
                    copied += n;
                }
                req.body[copied] = 0;
            }
            /* 路由 */
            http_route(root, &req, c);
        }
    }

    if (req.body) free(req.body);
    free(buf);
    shutdown(c, SD_BOTH);
    closesocket(c);
    return 0;
}

int http_server_start(const char* root, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        mec_set_error("WSAStartup failed");
        return -1;
    }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return -1;
    int reuse = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 仅本机 */
    addr.sin_port = htons((u_short)port);

    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        mec_set_error("bind failed on port %d", port);
        closesocket(ls);
        WSACleanup();
        return -1;
    }
    if (listen(ls, 32) != 0) {
        mec_set_error("listen failed");
        closesocket(ls);
        WSACleanup();
        return -1;
    }
    printf("[server] listening on http://127.0.0.1:%d\n", port);
    fflush(stdout);

    for (;;) {
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEINTR) continue;
            break;
        }
        ClientJob* job = (ClientJob*)malloc(sizeof(ClientJob));
        job->client = c;
        job->root = root;
        HANDLE h = CreateThread(NULL, 0, handle_client, job, 0, NULL);
        if (h) CloseHandle(h);
        else { closesocket(c); free(job); }
    }
    closesocket(ls);
    WSACleanup();
    return 0;
}
