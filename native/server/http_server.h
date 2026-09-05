/* http_server.h - 极简 HTTP 服务器（winsock2），支持 GET/POST、静态文件与 SSE 流式响应 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <winsock2.h>

typedef struct HttpRequest {
    char method[8];
    char path[2048];
    char query[1024];
    unsigned long content_length;
    char* body;               /* malloc 分配，NUL 结尾；无 body 时为 NULL */
    SOCKET client;
} HttpRequest;

/* 启动服务器（阻塞 accept 循环，内部每连接一线程）。返回 0 成功。 */
int http_server_start(const char* root, int port);

/* ---- 响应工具 ---- */
void http_send_status(SOCKET c, int code, const char* text);
void http_send_headers(SOCKET c, const char* content_type, unsigned long length, const char* extra);
/* 流式响应头：不含 Content-Length（配合 Connection: close 使用 close-delimited） */
void http_send_headers_stream(SOCKET c, const char* content_type, const char* extra);
void http_send_body(SOCKET c, const char* data, unsigned long len);
void http_send_json(SOCKET c, int code, const char* text);
void http_send_error(SOCKET c, int code, const char* msg);

/* 路由分发（routes.c 实现） */
void http_route(const char* root, HttpRequest* req, SOCKET client);

#endif
