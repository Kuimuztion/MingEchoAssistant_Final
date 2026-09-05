#include "mec_common.h"

#define MAX_CHAR_PROFILES 256

static MecCharacterProfile g_profiles[MAX_CHAR_PROFILES];
static int g_profile_count = 0;
static int g_echo_count = 0;
static int g_weapon_count = 0;
static int g_resonance_count = 0;
static int g_resonance_echo_count = 0;

static void default_profile(MecCharacterProfile* p, const char* name) {
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name ? name : "通用角色");
    snprintf(p->role, sizeof(p->role), "通用");
    p->target_crit_rate = 70.0f;
    p->target_crit_damage = 250.0f;
    p->target_attack = 2200.0f;
    p->target_energy = 120.0f;
    p->weight_basic = 0.10f;
    p->weight_heavy = 0.10f;
    p->weight_skill = 0.25f;
    p->weight_liberation = 0.35f;
    p->weight_attack = 0.20f;
}

static int parse_profile_line(char* line, MecCharacterProfile* out) {
    /* CHAR|name|role|crit|cdmg|atk|energy|basic|heavy|skill|lib|atk_weight */
    char* parts[16];
    int n = 0;
    char* tok = strtok(line, "|");
    while (tok && n < 16) { parts[n++] = tok; tok = strtok(NULL, "|"); }
    if (n < 12 || strcmp(parts[0], "CHAR") != 0) return 0;
    default_profile(out, parts[1]);
    snprintf(out->role, sizeof(out->role), "%s", parts[2]);
    out->target_crit_rate = (float)atof(parts[3]);
    out->target_crit_damage = (float)atof(parts[4]);
    out->target_attack = (float)atof(parts[5]);
    out->target_energy = (float)atof(parts[6]);
    out->weight_basic = (float)atof(parts[7]);
    out->weight_heavy = (float)atof(parts[8]);
    out->weight_skill = (float)atof(parts[9]);
    out->weight_liberation = (float)atof(parts[10]);
    out->weight_attack = (float)atof(parts[11]);
    return 1;
}

static int count_lines_with_prefix(const char* path, const char* prefix) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    int count = 0;
    char line[1024];
    size_t plen = strlen(prefix);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, prefix, plen) == 0) count++;
    }
    fclose(f);
    return count;
}

int mec_db_load(const char* root_dir) {
    g_profile_count = 0;
    g_echo_count = g_weapon_count = g_resonance_count = g_resonance_echo_count = 0;

    char dbdir[MEC_PATH_MAX];
    mec_path_join(dbdir, sizeof(dbdir), root_dir ? root_dir : ".", "database");

    char path[MEC_PATH_MAX];
    mec_path_join(path, sizeof(path), dbdir, "characters.bin");
    FILE* f = fopen(path, "rb");
    if (!f) {
        MecCharacterProfile p;
        default_profile(&p, "通用角色");
        g_profiles[g_profile_count++] = p;
        mec_set_error("database not found: %s", path);
        return MEC_ERR;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f) && g_profile_count < MAX_CHAR_PROFILES) {
        mec_trim(line);
        if (!line[0] || line[0] == '#') continue;
        MecCharacterProfile p;
        if (parse_profile_line(line, &p)) g_profiles[g_profile_count++] = p;
    }
    fclose(f);

    mec_path_join(path, sizeof(path), dbdir, "echoes.bin");
    g_echo_count = count_lines_with_prefix(path, "ECHO|");
    mec_path_join(path, sizeof(path), dbdir, "weapons.bin");
    g_weapon_count = count_lines_with_prefix(path, "WEAPON|");
    mec_path_join(path, sizeof(path), dbdir, "resonance.bin");
    g_resonance_count = count_lines_with_prefix(path, "RESONANCE|");
    mec_path_join(path, sizeof(path), dbdir, "resonance_echoes.bin");
    g_resonance_echo_count = count_lines_with_prefix(path, "SET|");

    if (g_profile_count == 0) {
        MecCharacterProfile p;
        default_profile(&p, "通用角色");
        g_profiles[g_profile_count++] = p;
    }
    return MEC_OK;
}

int mec_db_find_character(const char* name, MecCharacterProfile* out_profile) {
    if (!out_profile) return MEC_ERR_BAD_ARG;

    /* 优先实时读 character_profiles.csv（用户可直接编辑，刷新即生效）
       列: name,role,crit,crit_damage,attack,energy,basic,heavy,skill,liberation,attack_weight */
    {
        char csv[MEC_PATH_MAX];
        mec_path_join(csv, sizeof(csv), g_mec.database_dir, "character_profiles.csv");
        if (mec_csv_load_profile(csv, name ? name : "", out_profile) == 1) return MEC_OK;
    }

    if (!name || !name[0]) {
        *out_profile = g_profiles[0];
        return MEC_OK;
    }
    for (int i = 0; i < g_profile_count; ++i) {
        if (strcmp(g_profiles[i].name, name) == 0) {
            *out_profile = g_profiles[i];
            return MEC_OK;
        }
    }
    for (int i = 0; i < g_profile_count; ++i) {
        if (mec_utf8_contains(g_profiles[i].name, name) || mec_utf8_contains(name, g_profiles[i].name)) {
            *out_profile = g_profiles[i];
            return MEC_OK;
        }
    }
    MecCharacterProfile p;
    default_profile(&p, name);
    *out_profile = p;
    return MEC_ERR;
}

/* 实时读取 database/character_profiles.csv 中 name 对应的目标面板。
   命中返回 1，未命中返回 0。文件小（几十行），每次读成本可忽略。 */
int mec_csv_load_profile(const char* csv_path, const char* name, MecCharacterProfile* out) {
    if (!csv_path || !name || !out || !name[0]) return 0;
    FILE* f = fopen(csv_path, "rb");
    if (!f) return 0;
    char line[2048];
    int first = 1;
    int hit = 0;
    while (fgets(line, sizeof(line), f)) {
        if (first) {
            if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                memmove(line, line + 3, strlen(line + 3) + 1);
            first = 0;
            if (strstr(line, "name") && strstr(line, "role")) continue; /* 表头 */
        }
        char* save = NULL;
        char* parts[16]; int n = 0;
        char* tok = strtok_r(line, ",\r\n", &save);
        while (tok && n < 16) { parts[n++] = tok; tok = strtok_r(NULL, ",\r\n", &save); }
        if (n < 11 || !parts[0][0]) continue;
        mec_trim(parts[0]);
        if (strcmp(parts[0], name) != 0) continue;
        hit = 1;
        memset(out, 0, sizeof(*out));
        snprintf(out->name, sizeof(out->name), "%s", parts[0]);
        snprintf(out->role, sizeof(out->role), "%s", parts[1]);
        out->target_crit_rate = (float)atof(parts[2]);
        out->target_crit_damage = (float)atof(parts[3]);
        out->target_attack = (float)atof(parts[4]);
        out->target_energy = (float)atof(parts[5]);
        out->weight_basic = (float)atof(parts[6]);
        out->weight_heavy = (float)atof(parts[7]);
        out->weight_skill = (float)atof(parts[8]);
        out->weight_liberation = (float)atof(parts[9]);
        out->weight_attack = (float)atof(parts[10]);
        break;
    }
    fclose(f);
    return hit;
}

int mec_db_count_characters(void) { return g_profile_count; }
int mec_db_count_echoes(void) { return g_echo_count; }
int mec_db_count_weapons(void) { return g_weapon_count; }
int mec_db_count_resonance(void) { return g_resonance_count; }
int mec_db_count_resonance_echoes(void) { return g_resonance_echo_count; }
