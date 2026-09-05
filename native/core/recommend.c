#include "mec_common.h"

void mec_build_recommendation(const MecCharacterProfile* profile, const MecPanel* panel, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    if (!profile || !panel) {
        snprintf(out, (size_t)out_size, "无法生成推荐：缺少角色或面板信息。");
        return;
    }

    char buf[1024] = {0};
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "角色：%s｜定位：%s\n", profile->name, profile->role);

    float crit_gap = profile->target_crit_rate - panel->crit_rate;
    float cdmg_gap = profile->target_crit_damage - panel->crit_damage;
    float atk_gap = profile->target_attack - panel->attack;
    float energy_gap = profile->target_energy - panel->energy_regen;

    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "优先级：");
    int wrote = 0;
    if (crit_gap > 8.0f) { pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s暴击率", wrote ? " > " : ""); wrote = 1; }
    if (atk_gap > 250.0f) { pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s攻击/攻击%%", wrote ? " > " : ""); wrote = 1; }
    if (energy_gap > 10.0f) { pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s共鸣效率", wrote ? " > " : ""); wrote = 1; }
    if (cdmg_gap > 20.0f) { pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s暴击伤害", wrote ? " > " : ""); wrote = 1; }
    if (!wrote) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "有效双暴/攻击/关键伤害加成");

    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "。\n");

    if (panel->crit_damage >= profile->target_crit_damage && panel->crit_rate < profile->target_crit_rate) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "爆伤已经接近或超过目标，下一步不要盲目堆爆伤，优先补暴击率。\n");
    }
    if (panel->crit_rate >= profile->target_crit_rate && panel->crit_damage < profile->target_crit_damage) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "暴击率已经够用，后续可转向暴击伤害和攻击。\n");
    }
    if (panel->energy_regen > 0 && panel->energy_regen < profile->target_energy) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "共鸣效率低于目标，若循环卡手，带有共鸣效率的声骸价值上升。\n");
    }

    snprintf(out, (size_t)out_size, "%s", buf);
}

int mec_recommend_json(const char* character_name, const char* current_panel_text, char* out_json, int out_size) {
    if (!out_json || out_size <= 0) return MEC_ERR_BAD_ARG;
    MecCharacterProfile profile;
    mec_db_find_character(character_name ? character_name : "", &profile);
    MecStat stats[MEC_MAX_STATS];
    int n = mec_parse_stats(current_panel_text ? current_panel_text : "", stats, MEC_MAX_STATS);
    MecPanel panel;
    mec_panel_from_stats(stats, n > 0 ? n : 0, &panel);
    char rec[1024];
    mec_build_recommendation(&profile, &panel, rec, sizeof(rec));
    int pos = 0;
    mec_json_append(out_json, out_size, &pos, "{\"ok\":true,\"character\":\"");
    for (const char* p = profile.name; *p; ++p) {
        if (*p == '"' || *p == '\\') mec_json_append(out_json, out_size, &pos, "\\%c", *p);
        else mec_json_append(out_json, out_size, &pos, "%c", *p);
    }
    mec_json_append(out_json, out_size, &pos, "\",\"recommendation\":\"");
    for (const char* p = rec; *p; ++p) {
        if (*p == '"' || *p == '\\') mec_json_append(out_json, out_size, &pos, "\\%c", *p);
        else if (*p == '\n') mec_json_append(out_json, out_size, &pos, "\\n");
        else mec_json_append(out_json, out_size, &pos, "%c", *p);
    }
    mec_json_append(out_json, out_size, &pos, "\"}");
    return MEC_OK;
}
