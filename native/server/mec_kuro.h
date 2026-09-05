/* mec_kuro.h - 库街区 API 代理（curl 子进程）+ 账号配置 */
#ifndef MEC_KURO_H
#define MEC_KURO_H

typedef struct MecKuroConfig {
    char token[512];
    char role_id[64];
    char server_id[128];
} MecKuroConfig;

/* 加载 kuro_config.json；缺省返回 -1 */
int mec_kuro_load(const char* root, MecKuroConfig* cfg);
/* 保存 kuro_config.json */
int mec_kuro_save(const char* root, const MecKuroConfig* cfg);
/* 向库街区 API 发 POST，form-body 走临时文件，响应 JSON 写入 out。extra_headers 为附加 curl -H 参数（可 NULL） */
int mec_kuro_post(const char* path, const char* token, const char* form_body, const char* extra_headers, char* out, int out_size);

#endif
