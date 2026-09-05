/* mec_import.exe - C 版资源导入：扫描 resources/Project 生成 database/*.bin 与 catalog.json
   替代原 Python tools/import_project.py，本工具与运行时均不依赖 Python。
   使用宽字符 API 处理中文路径。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "win_utf8.h"

#define MAX_ITEMS 1024
#define MAX_PATH_BUF 4096

typedef struct Echo {
    char name[128];
    char level[64];
    int cost;
    char image[512];
} Echo;

static char g_chars[MAX_ITEMS][128];
static int g_char_count = 0;
static Echo g_echoes[MAX_ITEMS];
static int g_echo_count = 0;
static char g_weapons[MAX_ITEMS][128];
static int g_weapon_count = 0;
static char g_resonance[MAX_ITEMS][128];
static int g_resonance_count = 0;

typedef struct EchoMap { char set[128]; int cost; char echo[128]; } EchoMap;
static EchoMap g_maps[MAX_ITEMS];
static int g_map_count = 0;

typedef struct Profile {
    char name[128];
    char role[64];
    float crit, cdmg, atk, en, wb, wh, ws, wl, wa;
    int used;
} Profile;
static Profile g_profiles[MAX_ITEMS];
static int g_profile_count = 0;

/* 去掉扩展名（找最后一个 '.'，保留前面） */
static void stem(const char* path, char* out, int out_size) {
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    const char* dot = strrchr(base, '.');
    int n = dot ? (int)(dot - base) : (int)strlen(base);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, base, n);
    out[n] = 0;
}

static int is_image(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return 0;
    if (_stricmp(dot, ".png") == 0 || _stricmp(dot, ".jpg") == 0 ||
        _stricmp(dot, ".jpeg") == 0 || _stricmp(dot, ".webp") == 0) return 1;
    return 0;
}

static void add_char(const char* name) {
    for (int i = 0; i < g_char_count; i++) if (strcmp(g_chars[i], name) == 0) return;
    if (g_char_count < MAX_ITEMS) snprintf(g_chars[g_char_count++], 128, "%s", name);
}
static void add_weapon(const char* name) {
    for (int i = 0; i < g_weapon_count; i++) if (strcmp(g_weapons[i], name) == 0) return;
    if (g_weapon_count < MAX_ITEMS) snprintf(g_weapons[g_weapon_count++], 128, "%s", name);
}
static void add_resonance(const char* name) {
    for (int i = 0; i < g_resonance_count; i++) if (strcmp(g_resonance[i], name) == 0) return;
    if (g_resonance_count < MAX_ITEMS) snprintf(g_resonance[g_resonance_count++], 128, "%s", name);
}

/* 递归扫描图片（宽字符 FindFirstFileW）；rel 是相对 resources/Project 的目录前缀 */
static void scan_images(const char* dir, const char* rel, int kind) {
    char pat_u8[MAX_PATH_BUF];
    snprintf(pat_u8, sizeof(pat_u8), "%s\\*", dir);
    wchar_t wpat[2048];
    if (!utf8_to_wide(pat_u8, wpat, 2048)) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char name_u8[1024];
        if (!wide_to_utf8(fd.cFileName, name_u8, sizeof(name_u8))) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(name_u8, ".") == 0 || strcmp(name_u8, "..") == 0) continue;
            char subdir[MAX_PATH_BUF], subrel[MAX_PATH_BUF];
            snprintf(subdir, sizeof(subdir), "%s\\%s", dir, name_u8);
            snprintf(subrel, sizeof(subrel), "%s/%s", rel, name_u8);
            scan_images(subdir, subrel, kind);
        } else if (is_image(name_u8)) {
            char name[128];
            stem(name_u8, name, sizeof(name));
            if (kind == 0) {
                add_char(name);
            } else if (kind == 1) {
                if (g_echo_count < MAX_ITEMS) {
                    snprintf(g_echoes[g_echo_count].name, 128, "%s", name);
                    const char* lvl = strrchr(rel, '/');
                    snprintf(g_echoes[g_echo_count].level, 64, "%s", lvl ? lvl + 1 : "");
                    int cost = 0;
                    if (strstr(rel, "海啸级") || strstr(rel, "怒涛级")) cost = 4;
                    else if (strstr(rel, "巨浪级")) cost = 3;
                    else if (strstr(rel, "轻波级")) cost = 1;
                    g_echoes[g_echo_count].cost = cost;
                    snprintf(g_echoes[g_echo_count].image, 512, "resources/Project%s/%s", rel, name_u8);
                    g_echo_count++;
                }
            } else if (kind == 2) {
                add_weapon(name);
            } else if (kind == 3) {
                add_resonance(name);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 读取 character_profiles.csv（若存在）作为覆盖；表头:
   name,role,crit,crit_damage,attack,energy,basic,heavy,skill,liberation,attack_weight */
static void read_profiles_csv(const char* csv_path) {
    FILE* f = mec_fopen_utf8(csv_path, "rb");
    if (!f) return;
    char line[2048];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        if (first && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, strlen(line + 3) + 1);
        if (first) { first = 0; continue; }   /* header */
        char* save = NULL;
        char* parts[16]; int n = 0;
        char* tok = strtok_r(line, ",\r\n", &save);
        while (tok && n < 16) { parts[n++] = tok; tok = strtok_r(NULL, ",\r\n", &save); }
        if (n < 11 || !parts[0][0]) continue;
        if (g_profile_count >= MAX_ITEMS) break;
        Profile* p = &g_profiles[g_profile_count];
        snprintf(p->name, sizeof(p->name), "%s", parts[0]);
        snprintf(p->role, sizeof(p->role), "%s", parts[1]);
        p->crit = (float)atof(parts[2]);
        p->cdmg = (float)atof(parts[3]);
        p->atk = (float)atof(parts[4]);
        p->en = (float)atof(parts[5]);
        p->wb = (float)atof(parts[6]);
        p->wh = (float)atof(parts[7]);
        p->ws = (float)atof(parts[8]);
        p->wl = (float)atof(parts[9]);
        p->wa = (float)atof(parts[10]);
        p->used = 0;
        g_profile_count++;
    }
    fclose(f);
}

static Profile* find_profile(const char* name) {
    for (int i = 0; i < g_profile_count; i++)
        if (strcmp(g_profiles[i].name, name) == 0) { g_profiles[i].used = 1; return &g_profiles[i]; }
    return NULL;
}

/* 解析声骸合鸣列表 md */
static void parse_resonance_md(const char* md_path, const char* set_name) {
    FILE* f = mec_fopen_utf8(md_path, "rb");
    if (!f) return;
    char line[1024];
    int current_cost = 0;
    while (fgets(line, sizeof(line), f)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == '#' && (s[1] == ' ' || s[1] == '\t' || s[1] == 'c' || s[1] == 'C')) {
            const char* p = s + 1;
            while (*p == ' ' || *p == '\t' || *p == '#' || *p == 'c' || *p == 'C') p++;
            if (*p == '1' || *p == '3' || *p == '4') { current_cost = *p - '0'; continue; }
        }
        if (s[0] == '-' && current_cost) {
            char* name = s + 1;
            while (*name == ' ' || *name == '\t') name++;
            size_t n = strlen(name);
            while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r' || name[n - 1] == ' ')) name[--n] = 0;
            if (name[0] && g_map_count < MAX_ITEMS) {
                snprintf(g_maps[g_map_count].set, 128, "%s", set_name);
                g_maps[g_map_count].cost = current_cost;
                snprintf(g_maps[g_map_count].echo, 128, "%s", name);
                g_map_count++;
            }
        }
    }
    fclose(f);
}

static int cmp_echo(const void* a, const void* b) {
    const Echo* ea = (const Echo*)a;
    const Echo* eb = (const Echo*)b;
    if (ea->cost != eb->cost) return ea->cost - eb->cost;
    return strcmp(ea->name, eb->name);
}
static int cmp_str(const void* a, const void* b) {
    /* g_chars 等是 char[N][128]：元素即字符串首地址 */
    return strcmp((const char*)a, (const char*)b);
}

int main(int argc, char** argv) {
    char root[MAX_PATH_BUF] = ".";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) snprintf(root, sizeof(root), "%s", argv[++i]);
    }
    char project[MAX_PATH_BUF], dbdir[MAX_PATH_BUF];
    snprintf(project, sizeof(project), "%s\\resources\\Project", root);
    snprintf(dbdir, sizeof(dbdir), "%s\\database", root);
    {
        wchar_t wd[2048];
        if (utf8_to_wide(dbdir, wd, 2048)) CreateDirectoryW(wd, NULL);
    }

    char d[MAX_PATH_BUF];
    snprintf(d, sizeof(d), "%s\\角色", project); scan_images(d, "/角色", 0);
    snprintf(d, sizeof(d), "%s\\声骸", project); scan_images(d, "/声骸", 1);
    snprintf(d, sizeof(d), "%s\\武器", project); scan_images(d, "/武器", 2);
    snprintf(d, sizeof(d), "%s\\合鸣效果", project); scan_images(d, "/合鸣效果", 3);

    /* 声骸合鸣列表（宽字符遍历） */
    char md_dir[MAX_PATH_BUF];
    snprintf(md_dir, sizeof(md_dir), "%s\\声骸合鸣列表", project);
    {
        char pat_u8[MAX_PATH_BUF];
        snprintf(pat_u8, sizeof(pat_u8), "%s\\*.md", md_dir);
        wchar_t wpat[2048];
        if (utf8_to_wide(pat_u8, wpat, 2048)) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(wpat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    char name_u8[512];
                    if (!wide_to_utf8(fd.cFileName, name_u8, sizeof(name_u8))) continue;
                    char name[128];
                    stem(name_u8, name, sizeof(name));
                    char mp[MAX_PATH_BUF];
                    snprintf(mp, sizeof(mp), "%s\\%s", md_dir, name_u8);
                    parse_resonance_md(mp, name);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }

    /* 排序 */
    qsort(g_chars, g_char_count, sizeof(g_chars[0]), cmp_str);
    qsort(g_weapons, g_weapon_count, sizeof(g_weapons[0]), cmp_str);
    qsort(g_resonance, g_resonance_count, sizeof(g_resonance[0]), cmp_str);
    qsort(g_echoes, g_echo_count, sizeof(Echo), cmp_echo);

    /* 读已有 CSV 覆盖（保留用户自定义的毕业线） */
    char csv_path[MAX_PATH_BUF];
    snprintf(csv_path, sizeof(csv_path), "%s\\character_profiles.csv", dbdir);
    read_profiles_csv(csv_path);

    char p[MAX_PATH_BUF];
    FILE* f;

    /* characters.bin */
    snprintf(p, sizeof(p), "%s\\characters.bin", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "# MEADB characters v1\n");
        for (int i = 0; i < g_char_count; i++) {
            const char* name = g_chars[i];
            Profile* pr = find_profile(name);
            const char* role = "通用";
            float crit = 70, cdmg = 250, atk = 2200, en = 120;
            float wb = 0.10f, wh = 0.10f, ws = 0.25f, wl = 0.35f, wa = 0.20f;
            if (pr) {
                role = pr->role[0] ? pr->role : role;
                crit = pr->crit; cdmg = pr->cdmg; atk = pr->atk; en = pr->en;
                wb = pr->wb; wh = pr->wh; ws = pr->ws; wl = pr->wl; wa = pr->wa;
            } else if (strstr(name, "今汐")) {
                role = "主C"; crit = 75; cdmg = 260; atk = 2400; en = 120;
                wb = 0.10f; wh = 0.05f; ws = 0.30f; wl = 0.50f; wa = 0.20f;
            }
            fprintf(f, "CHAR|%s|%s|%.0f|%.0f|%.0f|%.0f|%.2f|%.2f|%.2f|%.2f|%.2f\n",
                    name, role, crit, cdmg, atk, en, wb, wh, ws, wl, wa);
        }
        fclose(f);
    }

    /* character_profiles.csv（回写，含新角色，便于用户编辑） */
    snprintf(p, sizeof(p), "%s\\character_profiles.csv", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "name,role,crit,crit_damage,attack,energy,basic,heavy,skill,liberation,attack_weight\r\n");
        for (int i = 0; i < g_char_count; i++) {
            Profile* pr = find_profile(g_chars[i]);
            if (pr) {
                fprintf(f, "%s,%s,%.0f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f\r\n",
                        pr->name, pr->role, pr->crit, pr->cdmg, pr->atk, pr->en,
                        pr->wb, pr->wh, pr->ws, pr->wl, pr->wa);
            } else {
                const char* role = "通用";
                float crit = 70, cdmg = 250, atk = 2200, en = 120;
                float wb = 0.10f, wh = 0.10f, ws = 0.25f, wl = 0.35f, wa = 0.20f;
                if (strstr(g_chars[i], "今汐")) {
                    role = "主C"; crit = 75; cdmg = 260; atk = 2400; en = 120;
                    wb = 0.10f; wh = 0.05f; ws = 0.30f; wl = 0.50f; wa = 0.20f;
                }
                fprintf(f, "%s,%s,%.0f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f\r\n",
                        g_chars[i], role, crit, cdmg, atk, en, wb, wh, ws, wl, wa);
            }
        }
        fclose(f);
    }

    /* echoes.bin */
    snprintf(p, sizeof(p), "%s\\echoes.bin", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "# MEADB echoes v1\n");
        for (int i = 0; i < g_echo_count; i++) {
            fprintf(f, "ECHO|%s|%d|%s|%s\n", g_echoes[i].name, g_echoes[i].cost, g_echoes[i].level, g_echoes[i].image);
        }
        fclose(f);
    }

    /* weapons.bin */
    snprintf(p, sizeof(p), "%s\\weapons.bin", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "# MEADB weapons v1\n");
        for (int i = 0; i < g_weapon_count; i++) fprintf(f, "WEAPON|%s\n", g_weapons[i]);
        fclose(f);
    }

    /* resonance.bin */
    snprintf(p, sizeof(p), "%s\\resonance.bin", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "# MEADB resonance v1\n");
        for (int i = 0; i < g_resonance_count; i++) fprintf(f, "RESONANCE|%s\n", g_resonance[i]);
        fclose(f);
    }

    /* resonance_echoes.bin */
    snprintf(p, sizeof(p), "%s\\resonance_echoes.bin", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "# MEADB resonance echoes v1\n");
        for (int i = 0; i < g_map_count; i++) fprintf(f, "SET|%s|%d|%s\n", g_maps[i].set, g_maps[i].cost, g_maps[i].echo);
        fclose(f);
    }

    /* 轻量 catalog.json */
    snprintf(p, sizeof(p), "%s\\catalog.json", dbdir);
    f = mec_fopen_utf8(p, "wb");
    if (f) {
        fprintf(f, "{\n\"counts\":{\"characters\":%d,\"echoes\":%d,\"weapons\":%d,\"resonance\":%d,\"resonance_echoes\":%d},\n\"characters\":[",
                g_char_count, g_echo_count, g_weapon_count, g_resonance_count, g_map_count);
        for (int i = 0; i < g_char_count; i++) fprintf(f, "%s\"%s\"", i ? "," : "", g_chars[i]);
        fprintf(f, "]\n}\n");
        fclose(f);
    }

    printf("Imported Project resources: characters=%d echoes=%d weapons=%d resonance=%d resonance_echoes=%d\n",
           g_char_count, g_echo_count, g_weapon_count, g_resonance_count, g_map_count);
    return 0;
}
