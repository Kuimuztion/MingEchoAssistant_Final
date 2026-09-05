#include "mec_config.h"
#include "win_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 找到 key 的 "key": 位置；key 必须被双引号包裹。返回 value 起始指针或 NULL。 */
static const char* find_key(const char* json, const char* key) {
    if (!json || !key) return NULL;
    size_t klen = strlen(key);
    const char* p = json;
    while ((p = strstr(p, key)) != NULL) {
        /* 前面必须是 '"' */
        if (p == json || *(p - 1) == '"') {
            const char* after = p + klen;
            /* 后面必须紧接着 '"'，即 key 本身是完整引号串 */
            if (*after == '"') {
                const char* colon = after + 1;
                while (*colon == ' ' || *colon == '\t') colon++;
                if (*colon == ':') {
                    const char* val = colon + 1;
                    while (*val == ' ' || *val == '\t' || *val == '\r' || *val == '\n') val++;
                    return val;
                }
            }
        }
        p++;
    }
    return NULL;
}

int mec_json_get_string(const char* json, const char* key, char* out, int out_size) {
    if (!out || out_size <= 0) return -1;
    out[0] = 0;
    const char* v = find_key(json, key);
    if (!v) return -1;
    if (*v != '"') return -1;   /* 不是字符串 */
    v++;
    int n = 0;
    while (*v && *v != '"' && n < out_size - 1) {
        if (*v == '\\' && v[1]) {
            v++;
            switch (*v) {
                case 'n': out[n++] = '\n'; break;
                case 't': out[n++] = '\t'; break;
                case 'r': out[n++] = '\r'; break;
                case '"': out[n++] = '"'; break;
                case '\\': out[n++] = '\\'; break;
                case '/': out[n++] = '/'; break;
                case 'u': {   /* \uXXXX -> UTF-8 */
                    unsigned int cp = 0;
                    int ok = 1;
                    for (int k = 1; k <= 4; k++) {
                        char c = v[k];
                        cp <<= 4;
                        if (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
                        else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
                        else { ok = 0; break; }
                    }
                    if (ok && cp) {
                        if (cp < 0x80) {
                            if (n < out_size - 1) out[n++] = (char)cp;
                        } else if (cp < 0x800) {
                            if (n + 1 < out_size - 1) {
                                out[n++] = (char)(0xC0 | (cp >> 6));
                                out[n++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else {
                            if (n + 2 < out_size - 1) {
                                out[n++] = (char)(0xE0 | (cp >> 12));
                                out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                out[n++] = (char)(0x80 | (cp & 0x3F));
                            }
                        }
                        v += 4;
                    } else {
                        out[n++] = 'u';
                    }
                    break;
                }
                default: out[n++] = *v; break;
            }
        } else {
            out[n++] = *v;
        }
        v++;
    }
    out[n] = 0;
    return n >= 0 ? 0 : -1;
}

int mec_json_get_int(const char* json, const char* key, int* out) {
    if (!out) return -1;
    const char* v = find_key(json, key);
    if (!v) return -1;
    if (*v == '"') return -1;
    *out = atoi(v);
    return 0;
}

int mec_json_get_float(const char* json, const char* key, float* out) {
    if (!out) return -1;
    const char* v = find_key(json, key);
    if (!v) return -1;
    if (*v == '"') return -1;
    *out = (float)atof(v);
    return 0;
}

int mec_config_load(const char* root, MecCloudConfig* cfg) {
    /* 默认值 */
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->base_url, sizeof(cfg->base_url), "https://apihub.agnes-ai.com/v1");
    snprintf(cfg->model, sizeof(cfg->model), "agnes-2.5-flash");
    cfg->auto_start = 1;
    cfg->timeout = 90;
    cfg->max_tokens = 4096;
    cfg->temperature = 0.7f;
    cfg->top_p = 0.95f;

    char path[2048];
    snprintf(path, sizeof(path), "%s%cconfig.json", root ? root : ".", 
             (root && root[0] && root[strlen(root) - 1] != '\\' && root[strlen(root) - 1] != '/') ? '\\' : '\0');
    if (!root || !root[0]) snprintf(path, sizeof(path), "config.json");

    FILE* f = mec_fopen_utf8(path, "rb");
    if (!f) return -1;
    char* buf = (char*)malloc(1 << 20);
    size_t n = fread(buf, 1, 1 << 20, f);
    fclose(f);
    buf[n] = 0;

    mec_json_get_string(buf, "base_url", cfg->base_url, (int)sizeof(cfg->base_url));
    mec_json_get_string(buf, "model", cfg->model, (int)sizeof(cfg->model));
    mec_json_get_string(buf, "api_key", cfg->api_key, (int)sizeof(cfg->api_key));
    mec_json_get_string(buf, "system_prompt", cfg->system_prompt, (int)sizeof(cfg->system_prompt));
    mec_json_get_int(buf, "auto_start", &cfg->auto_start);
    mec_json_get_int(buf, "timeout", &cfg->timeout);
    mec_json_get_int(buf, "max_tokens", &cfg->max_tokens);
    mec_json_get_float(buf, "temperature", &cfg->temperature);
    mec_json_get_float(buf, "top_p", &cfg->top_p);
    free(buf);
    return 0;
}

/* 把 config.json 的 api_key 字段替换为 new_key；不存在则追加。旧值被覆盖。 */
int mec_config_set_api_key(const char* root, const char* new_key) {
    if (!new_key) return -1;
    char path[2048];
    snprintf(path, sizeof(path), "%s%cconfig.json", root ? root : ".",
             (root && root[0] && root[strlen(root) - 1] != '\\' && root[strlen(root) - 1] != '/') ? '\\' : '\0');
    if (!root || !root[0]) snprintf(path, sizeof(path), "config.json");

    FILE* f = mec_fopen_utf8(path, "rb");
    if (!f) return -1;
    char* buf = (char*)malloc(1 << 20);
    size_t n = fread(buf, 1, (1 << 20) - 1, f);
    fclose(f);
    buf[n] = 0;

    char* out = (char*)malloc(1 << 20);
    int out_len = 0;
    const char* v = find_key(buf, "api_key");

    if (v && *v == '"') {
        /* 替换已有字段的值 */
        const char* val_start = v + 1;
        const char* val_end = val_start;
        while (*val_end && *val_end != '"') {
            if (*val_end == '\\' && val_end[1]) val_end += 2;
            else val_end++;
        }
        int prefix = (int)(val_start - buf);
        memcpy(out, buf, (size_t)prefix);
        out_len = prefix;
        out_len += snprintf(out + out_len, (1 << 20) - out_len, "%s", new_key);
        int suffix = (int)(n - (size_t)(val_end - buf));
        memcpy(out + out_len, val_end, (size_t)suffix);
        out_len += suffix;
    } else {
        /* 字段不存在，在末尾 } 前追加 */
        const char* close = strrchr(buf, '}');
        if (!close) { free(buf); free(out); return -1; }
        int prefix = (int)(close - buf);
        const char* p = close - 1;
        while (p > buf && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p--;
        int need_comma = (p > buf && *p != '{' && *p != ',');
        memcpy(out, buf, (size_t)prefix);
        out_len = prefix;
        if (need_comma) out[out_len++] = ',';
        out[out_len++] = '\n';
        out_len += snprintf(out + out_len, (1 << 20) - out_len, "  \"api_key\": \"%s\"\n", new_key);
        out[out_len++] = '}';
        out[out_len] = 0;
    }

    f = mec_fopen_utf8(path, "wb");
    if (!f) { free(buf); free(out); return -1; }
    fwrite(out, 1, (size_t)out_len, f);
    fclose(f);
    free(buf);
    free(out);
    return 0;
}
