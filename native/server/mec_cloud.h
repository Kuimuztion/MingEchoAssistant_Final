/* cloud.h - Agnes AI 云端对话代理：把浏览器请求转发到云端并流式回传 */
#ifndef MEC_CLOUD_H
#define MEC_CLOUD_H

#include <winsock2.h>
#include "mec_config.h"

/* 用 WinHTTP 请求云端 /chat/completions（stream），把 SSE 流转发给浏览器。
   request_json 为前端传来的 {"messages": [...]}。
   返回 0 成功（已发送 SSE 头），非 0 失败（可能已发送错误事件）。 */
int cloud_chat_stream(const MecCloudConfig* cfg, const char* request_json, SOCKET client);

#endif
