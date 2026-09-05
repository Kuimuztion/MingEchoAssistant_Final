#include "mec_common.h"

typedef struct StatPattern {
    const char* key;
    MecStatType type;
    int percent_hint;
} StatPattern;

static const StatPattern G_PATTERNS[] = {
    {"共鸣解放伤害加成", MEC_STAT_LIBERATION_DMG, 1},
    {"共鸣解放", MEC_STAT_LIBERATION_DMG, 1},
    {"解放伤害", MEC_STAT_LIBERATION_DMG, 1},
    {"共鸣技能伤害加成", MEC_STAT_SKILL_DMG, 1},
    {"共鸣技能", MEC_STAT_SKILL_DMG, 1},
    {"技能伤害", MEC_STAT_SKILL_DMG, 1},
    {"普通攻击伤害加成", MEC_STAT_BASIC_DMG, 1},
    {"普攻伤害", MEC_STAT_BASIC_DMG, 1},
    {"普通攻击", MEC_STAT_BASIC_DMG, 1},
    {"普攻", MEC_STAT_BASIC_DMG, 1},
    {"重击伤害加成", MEC_STAT_HEAVY_DMG, 1},
    {"重击伤害", MEC_STAT_HEAVY_DMG, 1},
    {"重击", MEC_STAT_HEAVY_DMG, 1},
    {"衍射伤害加成", MEC_STAT_ELEMENT_DMG, 1},
    {"热熔伤害加成", MEC_STAT_ELEMENT_DMG, 1},
    {"导电伤害加成", MEC_STAT_ELEMENT_DMG, 1},
    {"冷凝伤害加成", MEC_STAT_ELEMENT_DMG, 1},
    {"气动伤害加成", MEC_STAT_ELEMENT_DMG, 1},
    {"湮灭伤害加成", MEC_STAT_ELEMENT_DMG, 1},
    {"衍射伤害", MEC_STAT_ELEMENT_DMG, 1},
    {"热熔伤害", MEC_STAT_ELEMENT_DMG, 1},
    {"导电伤害", MEC_STAT_ELEMENT_DMG, 1},
    {"冷凝伤害", MEC_STAT_ELEMENT_DMG, 1},
    {"气动伤害", MEC_STAT_ELEMENT_DMG, 1},
    {"湮灭伤害", MEC_STAT_ELEMENT_DMG, 1},
    {"治疗效果加成", MEC_STAT_HEALING_BONUS, 1},
    {"治疗加成", MEC_STAT_HEALING_BONUS, 1},
    {"暴击伤害", MEC_STAT_CRIT_DAMAGE, 1},
    {"爆伤", MEC_STAT_CRIT_DAMAGE, 1},
    {"暴击率", MEC_STAT_CRIT_RATE, 1},
    {"暴击", MEC_STAT_CRIT_RATE, 1},
    {"共鸣效率", MEC_STAT_ENERGY_REGEN, 1},
    {"充能效率", MEC_STAT_ENERGY_REGEN, 1},
    {"充能", MEC_STAT_ENERGY_REGEN, 1},
    {"攻击力", MEC_STAT_ATTACK_FLAT, 0},
    {"攻击", MEC_STAT_ATTACK_FLAT, 0},
    {"生命值", MEC_STAT_HP_FLAT, 0},
    {"生命", MEC_STAT_HP_FLAT, 0},
    {"防御力", MEC_STAT_DEF_FLAT, 0},
    {"防御", MEC_STAT_DEF_FLAT, 0},
    {NULL, MEC_STAT_UNKNOWN, 0}
};

typedef struct Candidate {
    int pos;
    int len;
    MecStatType type;
    int percent_hint;
} Candidate;

static int contains_percent_near(const char* p) {
    if (!p) return 0;
    for (int i = 0; p[i] && i < 16; ++i) {
        if (p[i] == '%') return 1;
        if ((unsigned char)p[i] == 0xEF && (unsigned char)p[i + 1] == 0xBC && (unsigned char)p[i + 2] == 0x85) return 1; /* ％ */
    }
    return 0;
}

float mec_parse_first_number(const char* line, int* found, int* is_percent) {
    if (found) *found = 0;
    if (is_percent) *is_percent = 0;
    if (!line) return 0.0f;
    const char* p = line;
    while (*p) {
        if ((*p >= '0' && *p <= '9') || ((*p == '-' || *p == '+') && p[1] >= '0' && p[1] <= '9')) {
            char* endp = NULL;
            double v = strtod(p, &endp);
            if (endp != p) {
                if (found) *found = 1;
                while (*endp && isspace((unsigned char)*endp)) endp++;
                if (is_percent && contains_percent_near(endp)) *is_percent = 1;
                return (float)v;
            }
        }
        p++;
    }
    return 0.0f;
}

static int overlaps_existing(const Candidate* cs, int n, int pos, int len) {
    for (int i = 0; i < n; ++i) {
        int a0 = cs[i].pos;
        int a1 = cs[i].pos + cs[i].len;
        int b0 = pos;
        int b1 = pos + len;
        if (b0 < a1 && a0 < b1) return 1;
    }
    return 0;
}

static int cmp_candidate(const void* a, const void* b) {
    const Candidate* x = (const Candidate*)a;
    const Candidate* y = (const Candidate*)b;
    return x->pos - y->pos;
}

static int collect_candidates(const char* text, Candidate* out, int max_out) {
    int count = 0;
    if (!text) return 0;
    for (const StatPattern* pat = G_PATTERNS; pat->key; ++pat) {
        const char* p = text;
        size_t klen = strlen(pat->key);
        while ((p = strstr(p, pat->key)) != NULL) {
            int pos = (int)(p - text);
            if (count < max_out && !overlaps_existing(out, count, pos, (int)klen)) {
                out[count].pos = pos;
                out[count].len = (int)klen;
                out[count].type = pat->type;
                out[count].percent_hint = pat->percent_hint;
                count++;
            }
            p += klen;
        }
    }
    qsort(out, (size_t)count, sizeof(Candidate), cmp_candidate);
    return count;
}

static int add_stat_from_window(const char* window, MecStatType type, int percent_hint, MecStat* out, int* count, int max_stats) {
    int found = 0, pct = 0;
    float value = mec_parse_first_number(window, &found, &pct);
    if (!found || type == MEC_STAT_UNKNOWN || *count >= max_stats) return 0;

    MecStatType final_type = type;
    int final_pct = pct || percent_hint;
    if (type == MEC_STAT_ATTACK_FLAT && final_pct) final_type = MEC_STAT_ATTACK_PERCENT;
    if (type == MEC_STAT_HP_FLAT && final_pct) final_type = MEC_STAT_HP_PERCENT;
    if (type == MEC_STAT_DEF_FLAT && final_pct) final_type = MEC_STAT_DEF_PERCENT;

    MecStat* st = &out[(*count)++];
    memset(st, 0, sizeof(*st));
    snprintf(st->name, sizeof(st->name), "%s", mec_stat_type_name(final_type));
    st->type = final_type;
    st->value = value;
    st->is_percent = final_pct;
    return 1;
}

static int parse_chunk(const char* chunk, MecStat* out_stats, int* count, int max_stats) {
    Candidate cs[64];
    int cn = collect_candidates(chunk, cs, 64);
    if (cn <= 0) return 0;
    int added = 0;
    int len = (int)strlen(chunk);
    for (int i = 0; i < cn && *count < max_stats; ++i) {
        int start = cs[i].pos;
        int end = (i + 1 < cn) ? cs[i + 1].pos : len;
        if (end <= start) end = start + 96;
        if (end > len) end = len;
        if (end - start > 128) end = start + 128;
        char window[160];
        int n = end - start;
        if (n > (int)sizeof(window) - 1) n = (int)sizeof(window) - 1;
        memcpy(window, chunk + start, (size_t)n);
        window[n] = 0;
        added += add_stat_from_window(window, cs[i].type, cs[i].percent_hint, out_stats, count, max_stats);
    }
    return added;
}

int mec_parse_stats(const char* text, MecStat* out_stats, int max_stats) {
    if (!text || !out_stats || max_stats <= 0) return MEC_ERR_BAD_ARG;
    int count = 0;
    memset(out_stats, 0, sizeof(MecStat) * (size_t)max_stats);

    const char* p = text;
    char line[1024];
    while (*p && count < max_stats) {
        int n = 0;
        while (*p && *p != '\n' && *p != '\r' && n < (int)sizeof(line) - 1) line[n++] = *p++;
        while (*p == '\n' || *p == '\r' || *p == ';') p++;
        line[n] = 0;
        mec_trim(line);
        if (!line[0]) continue;
        parse_chunk(line, out_stats, &count, max_stats);
    }
    return count;
}

void mec_panel_from_stats(const MecStat* stats, int count, MecPanel* panel) {
    if (!panel) return;
    memset(panel, 0, sizeof(*panel));
    for (int i = 0; i < count; ++i) {
        const MecStat* s = &stats[i];
        switch (s->type) {
            case MEC_STAT_CRIT_RATE: panel->crit_rate = s->value; break;
            case MEC_STAT_CRIT_DAMAGE: panel->crit_damage = s->value; break;
            case MEC_STAT_ATTACK_FLAT: if (s->value > panel->attack) panel->attack = s->value; break;
            case MEC_STAT_ATTACK_PERCENT: if (panel->attack <= 0) panel->attack = 2200.0f * (1.0f + s->value / 100.0f); break;
            case MEC_STAT_ENERGY_REGEN: panel->energy_regen = s->value; break;
            case MEC_STAT_ELEMENT_DMG: panel->element_damage = s->value; break;
            case MEC_STAT_SKILL_DMG: panel->skill_damage = s->value; break;
            case MEC_STAT_LIBERATION_DMG: panel->liberation_damage = s->value; break;
            case MEC_STAT_BASIC_DMG: panel->basic_damage = s->value; break;
            case MEC_STAT_HEAVY_DMG: panel->heavy_damage = s->value; break;
            case MEC_STAT_HP_FLAT: if (s->value > panel->hp) panel->hp = s->value; break;
            case MEC_STAT_DEF_FLAT: if (s->value > panel->defense) panel->defense = s->value; break;
            default: break;
        }
    }
}

static void json_escape_append(char* out, int out_size, int* pos, const char* s) {
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') mec_json_append(out, out_size, pos, "\\%c", c);
        else if (c == '\n') mec_json_append(out, out_size, pos, "\\n");
        else if (c == '\r') {}
        else mec_json_append(out, out_size, pos, "%c", c);
    }
}

int mec_parse_stats_json(const char* text, char* out_json, int out_size) {
    if (!out_json || out_size <= 0) return MEC_ERR_BAD_ARG;
    MecStat stats[MEC_MAX_STATS];
    int count = mec_parse_stats(text ? text : "", stats, MEC_MAX_STATS);
    if (count < 0) return mec_write_json_error(out_json, out_size, "parse failed");
    int pos = 0;
    if (mec_json_append(out_json, out_size, &pos, "{\"ok\":true,\"count\":%d,\"stats\":[", count) != MEC_OK) return MEC_ERR_BUFFER_SMALL;
    for (int i = 0; i < count; ++i) {
        if (i) mec_json_append(out_json, out_size, &pos, ",");
        mec_json_append(out_json, out_size, &pos, "{\"name\":\"");
        json_escape_append(out_json, out_size, &pos, stats[i].name);
        mec_json_append(out_json, out_size, &pos, "\",\"type\":%d,\"value\":%.3f,\"percent\":%s}", (int)stats[i].type, stats[i].value, stats[i].is_percent ? "true" : "false");
    }
    mec_json_append(out_json, out_size, &pos, "]}");
    return MEC_OK;
}
