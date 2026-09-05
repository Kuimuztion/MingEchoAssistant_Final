#include "mec_common.h"

static float stat_base_value(MecStatType type, float value) {
    switch (type) {
        case MEC_STAT_CRIT_RATE: return value * 2.0f;
        case MEC_STAT_CRIT_DAMAGE: return value * 1.0f;
        case MEC_STAT_ATTACK_PERCENT: return value * 1.25f;
        case MEC_STAT_ATTACK_FLAT: return value / 18.0f;
        case MEC_STAT_ENERGY_REGEN: return value * 1.15f;
        case MEC_STAT_LIBERATION_DMG: return value * 0.95f;
        case MEC_STAT_SKILL_DMG: return value * 0.95f;
        case MEC_STAT_BASIC_DMG: return value * 0.85f;
        case MEC_STAT_HEAVY_DMG: return value * 0.85f;
        case MEC_STAT_ELEMENT_DMG: return value * 1.05f;
        case MEC_STAT_HP_PERCENT:
        case MEC_STAT_DEF_PERCENT:
        case MEC_STAT_HP_FLAT:
        case MEC_STAT_DEF_FLAT:
        case MEC_STAT_HEALING_BONUS:
        default: return value * 0.25f;
    }
}

static float stat_context_multiplier(const MecCharacterProfile* profile, const MecPanel* panel, MecStatType type) {
    float m = 1.0f;
    if (!profile || !panel) return m;
    float crit_gap = profile->target_crit_rate - panel->crit_rate;
    float cdmg_gap = profile->target_crit_damage - panel->crit_damage;
    float atk_gap = profile->target_attack - panel->attack;
    float energy_gap = profile->target_energy - panel->energy_regen;

    switch (type) {
        case MEC_STAT_CRIT_RATE:
            if (crit_gap > 12.0f) m += 0.45f;
            else if (crit_gap > 5.0f) m += 0.25f;
            else if (crit_gap < -5.0f) m -= 0.20f;
            break;
        case MEC_STAT_CRIT_DAMAGE:
            if (cdmg_gap > 35.0f) m += 0.30f;
            else if (cdmg_gap < -20.0f && crit_gap > 8.0f) m -= 0.30f;
            break;
        case MEC_STAT_ATTACK_PERCENT:
        case MEC_STAT_ATTACK_FLAT:
            if (atk_gap > 350.0f) m += 0.25f;
            break;
        case MEC_STAT_ENERGY_REGEN:
            if (energy_gap > 12.0f) m += 0.35f;
            else if (energy_gap < -20.0f) m -= 0.25f;
            break;
        case MEC_STAT_LIBERATION_DMG: m += profile->weight_liberation; break;
        case MEC_STAT_SKILL_DMG: m += profile->weight_skill; break;
        case MEC_STAT_BASIC_DMG: m += profile->weight_basic; break;
        case MEC_STAT_HEAVY_DMG: m += profile->weight_heavy; break;
        default: break;
    }
    return mec_clampf(m, 0.35f, 1.75f);
}

float mec_score_stats_for_character(const MecCharacterProfile* profile, const MecStat* echo_stats, int echo_count, const MecPanel* panel, char* reason, int reason_size) {
    float raw = 0.0f;
    int useful = 0;
    for (int i = 0; i < echo_count; ++i) {
        float base = stat_base_value(echo_stats[i].type, echo_stats[i].value);
        float mul = stat_context_multiplier(profile, panel, echo_stats[i].type);
        float add = base * mul;
        raw += add;
        if (add >= 7.0f) useful++;
    }
    float score = mec_clampf(raw, 0.0f, 100.0f);
    if (reason && reason_size > 0) {
        const char* grade = score >= 90 ? "SS" : score >= 80 ? "S" : score >= 68 ? "A" : score >= 55 ? "B" : "C";
        snprintf(reason, (size_t)reason_size, "评分 %.1f / 100，评级 %s。有效词条 %d 个。评分已根据当前面板缺口动态调整。", score, grade, useful);
    }
    return score;
}

static const char* grade_from_score(float s) {
    if (s >= 90) return "SS";
    if (s >= 80) return "S";
    if (s >= 68) return "A";
    if (s >= 55) return "B";
    return "C";
}

static void append_escaped(char* out, int out_size, int* pos, const char* s) {
    while (s && *s) {
        if (*s == '"' || *s == '\\') mec_json_append(out, out_size, pos, "\\%c", *s);
        else if (*s == '\n') mec_json_append(out, out_size, pos, "\\n");
        else if (*s == '\r') {}
        else mec_json_append(out, out_size, pos, "%c", *s);
        s++;
    }
}

int mec_score_echo_json(const char* character_name, const char* echo_stats_text, const char* current_panel_text, char* out_json, int out_size) {
    if (!out_json || out_size <= 0) return MEC_ERR_BAD_ARG;
    MecCharacterProfile profile;
    mec_db_find_character(character_name ? character_name : "", &profile);

    MecStat echo_stats[MEC_MAX_STATS];
    int echo_count = mec_parse_stats(echo_stats_text ? echo_stats_text : "", echo_stats, MEC_MAX_STATS);
    if (echo_count < 0) echo_count = 0;

    MecStat panel_stats[MEC_MAX_STATS];
    int panel_count = mec_parse_stats(current_panel_text ? current_panel_text : "", panel_stats, MEC_MAX_STATS);
    if (panel_count < 0) panel_count = 0;
    MecPanel panel;
    mec_panel_from_stats(panel_stats, panel_count, &panel);

    char reason[512];
    float score = mec_score_stats_for_character(&profile, echo_stats, echo_count, &panel, reason, sizeof(reason));
    char rec[1024];
    mec_build_recommendation(&profile, &panel, rec, sizeof(rec));
    const char* grade = grade_from_score(score);

    /* 未提供声骸词条时：改为评估"面板完成度"（当前面板 vs 目标面板） */
    if (echo_count == 0) {
        float crit_p = profile.target_crit_rate > 0 ? panel.crit_rate / profile.target_crit_rate : 0.0f;
        float cdmg_p = profile.target_crit_damage > 0 ? panel.crit_damage / profile.target_crit_damage : 0.0f;
        float atk_p = profile.target_attack > 0 ? panel.attack / profile.target_attack : 0.0f;
        float en_p = profile.target_energy > 0 ? panel.energy_regen / profile.target_energy : 0.0f;
        crit_p = mec_clampf(crit_p, 0.0f, 1.2f);
        cdmg_p = mec_clampf(cdmg_p, 0.0f, 1.2f);
        atk_p = mec_clampf(atk_p, 0.0f, 1.2f);
        en_p = mec_clampf(en_p, 0.0f, 1.2f);
        float avg = (crit_p + cdmg_p + atk_p + en_p) / 4.0f;
        score = mec_clampf(avg * 100.0f, 0.0f, 100.0f);
        grade = grade_from_score(score);
        snprintf(reason, sizeof(reason),
            "面板完成度 %.1f%%（暴击 %.0f%% / 暴伤 %.0f%% / 攻击 %.0f%% / 共鸣效率 %.0f%%）。未填声骸词条，仅评估当前面板。",
            score, crit_p * 100.0f, cdmg_p * 100.0f, atk_p * 100.0f, en_p * 100.0f);
    }

    int pos = 0;
    mec_json_append(out_json, out_size, &pos, "{\"ok\":true,\"character\":\"");
    append_escaped(out_json, out_size, &pos, profile.name);
    mec_json_append(out_json, out_size, &pos, "\",\"role\":\"");
    append_escaped(out_json, out_size, &pos, profile.role);
    mec_json_append(out_json, out_size, &pos, "\",\"score\":%.2f,\"grade\":\"%s\",\"reason\":\"", score, grade);
    append_escaped(out_json, out_size, &pos, reason);
    mec_json_append(out_json, out_size, &pos, "\",\"recommendation\":\"");
    append_escaped(out_json, out_size, &pos, rec);
    mec_json_append(out_json, out_size, &pos, "\",\"parsed_stats\":[");
    for (int i = 0; i < echo_count; ++i) {
        if (i) mec_json_append(out_json, out_size, &pos, ",");
        mec_json_append(out_json, out_size, &pos, "{\"name\":\"");
        append_escaped(out_json, out_size, &pos, echo_stats[i].name);
        mec_json_append(out_json, out_size, &pos, "\",\"value\":%.3f,\"percent\":%s}", echo_stats[i].value, echo_stats[i].is_percent ? "true" : "false");
    }
    mec_json_append(out_json, out_size, &pos, "]}");
    return MEC_OK;
}
