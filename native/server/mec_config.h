/* mec_config.h - 读取 config.json（迷你 JSON 取值，仅覆盖本项目用到的字段） */
#ifndef MEC_CONFIG_H
#define MEC_CONFIG_H

typedef struct MecCloudConfig {
    char base_url[256];
    char model[128];
    char api_key[256];
    int  auto_start;
    int  timeout;
    int  max_tokens;
    float temperature;
    float top_p;
    char system_prompt[8192];
} MecCloudConfig;

/* 在 JSON 文本中取一个字符串字段（自动去引号/转义，就地写入 out） */
int mec_json_get_string(const char* json, const char* key, char* out, int out_size);
/* 取整数/浮点字段 */
int mec_json_get_int(const char* json, const char* key, int* out);
int mec_json_get_float(const char* json, const char* key, float* out);

/* 加载 config.json 到 cfg，缺省用默认值；返回 0 成功 */
int mec_config_load(const char* root, MecCloudConfig* cfg);

/* 把 config.json 里的 api_key 字段替换为 new_key（旧密钥从文件中彻底覆盖删除）；返回 0 成功 */
int mec_config_set_api_key(const char* root, const char* new_key);

#endif
