#include "mec_common.h"

MecContext g_mec;

const char* mec_version(void) {
    return "MingEchoAssistant_Final 1.0.0";
}

void mec_set_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_mec.last_error, sizeof(g_mec.last_error), fmt, ap);
    va_end(ap);
}

const char* mec_last_error(void) {
    if (g_mec.last_error[0] == '\0') return "";
    return g_mec.last_error;
}

void mec_path_join(char* out, size_t out_size, const char* a, const char* b) {
    if (!out || out_size == 0) return;
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a);
    if (la == 0) {
        snprintf(out, out_size, "%s", b);
        return;
    }
    char last = a[la - 1];
    if (last == '/' || last == '\\') snprintf(out, out_size, "%s%s", a, b);
    else snprintf(out, out_size, "%s%s%s", a, MEC_SEP, b);
}

int mec_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

char* mec_read_text_file(const char* path, size_t* out_size) {
    if (out_size) *out_size = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)calloc((size_t)n + 1, 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (out_size) *out_size = got;
    return buf;
}

int mec_json_append(char* out, int out_size, int* pos, const char* fmt, ...) {
    if (!out || !pos || *pos < 0 || *pos >= out_size) return MEC_ERR_BUFFER_SMALL;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *pos, (size_t)(out_size - *pos), fmt, ap);
    va_end(ap);
    if (n < 0 || *pos + n >= out_size) {
        if (out_size > 0) out[out_size - 1] = 0;
        return MEC_ERR_BUFFER_SMALL;
    }
    *pos += n;
    return MEC_OK;
}

int mec_write_json_error(char* out, int out_size, const char* message) {
    if (!out || out_size <= 0) return MEC_ERR_BAD_ARG;
    snprintf(out, (size_t)out_size, "{\"ok\":false,\"error\":\"%s\"}", message ? message : "error");
    return MEC_ERR;
}

void mec_trim(char* s) {
    if (!s) return;
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

float mec_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int mec_utf8_contains(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    return strstr(haystack, needle) != NULL;
}

int mec_init(const char* root_dir) {
    memset(&g_mec, 0, sizeof(g_mec));
    snprintf(g_mec.root, sizeof(g_mec.root), "%s", (root_dir && root_dir[0]) ? root_dir : ".");
    mec_path_join(g_mec.database_dir, sizeof(g_mec.database_dir), g_mec.root, "database");
    mec_path_join(g_mec.models_dir, sizeof(g_mec.models_dir), g_mec.root, "models");

    int db = mec_db_load(g_mec.root);
    if (db == MEC_OK) g_mec.db_loaded = 1;
    return MEC_OK;
}

void mec_shutdown(void) {
    memset(&g_mec, 0, sizeof(g_mec));
}
