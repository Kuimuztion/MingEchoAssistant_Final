#include "mec_common.h"

const char* mec_stat_type_name(MecStatType type) {
    switch (type) {
        case MEC_STAT_CRIT_RATE: return "暴击率";
        case MEC_STAT_CRIT_DAMAGE: return "暴击伤害";
        case MEC_STAT_ATTACK_FLAT: return "攻击";
        case MEC_STAT_ATTACK_PERCENT: return "攻击%";
        case MEC_STAT_HP_FLAT: return "生命";
        case MEC_STAT_HP_PERCENT: return "生命%";
        case MEC_STAT_DEF_FLAT: return "防御";
        case MEC_STAT_DEF_PERCENT: return "防御%";
        case MEC_STAT_ENERGY_REGEN: return "共鸣效率";
        case MEC_STAT_BASIC_DMG: return "普攻伤害加成";
        case MEC_STAT_HEAVY_DMG: return "重击伤害加成";
        case MEC_STAT_SKILL_DMG: return "共鸣技能伤害加成";
        case MEC_STAT_LIBERATION_DMG: return "共鸣解放伤害加成";
        case MEC_STAT_ELEMENT_DMG: return "属性伤害加成";
        case MEC_STAT_HEALING_BONUS: return "治疗效果加成";
        default: return "未知";
    }
}

static int has_any(const char* s, const char** words) {
    for (int i = 0; words[i]; ++i) {
        if (mec_utf8_contains(s, words[i])) return 1;
    }
    return 0;
}

MecStatType mec_detect_stat_type(const char* line, int* is_percent_hint) {
    if (is_percent_hint) *is_percent_hint = 0;
    if (!line) return MEC_STAT_UNKNOWN;

    const char* element_words[] = {"衍射伤害", "热熔伤害", "导电伤害", "冷凝伤害", "气动伤害", "湮灭伤害", "伤害加成", NULL};
    const char* basic_words[] = {"普攻", "普通攻击", NULL};
    const char* heavy_words[] = {"重击", NULL};
    const char* skill_words[] = {"共鸣技能", "技能伤害", NULL};
    const char* liberation_words[] = {"共鸣解放", "解放伤害", NULL};
    const char* energy_words[] = {"共鸣效率", "充能", "效率", NULL};
    const char* heal_words[] = {"治疗效果", "治疗加成", NULL};

    if (has_any(line, energy_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_ENERGY_REGEN; }
    if (has_any(line, liberation_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_LIBERATION_DMG; }
    if (has_any(line, skill_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_SKILL_DMG; }
    if (has_any(line, heavy_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_HEAVY_DMG; }
    if (has_any(line, basic_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_BASIC_DMG; }
    if (has_any(line, heal_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_HEALING_BONUS; }

    if (mec_utf8_contains(line, "暴击伤害") || mec_utf8_contains(line, "爆伤")) {
        if (is_percent_hint) *is_percent_hint = 1;
        return MEC_STAT_CRIT_DAMAGE;
    }
    if (mec_utf8_contains(line, "暴击率") || mec_utf8_contains(line, "暴击")) {
        if (is_percent_hint) *is_percent_hint = 1;
        return MEC_STAT_CRIT_RATE;
    }

    if (mec_utf8_contains(line, "攻击") || mec_utf8_contains(line, "攻击力")) {
        if (is_percent_hint) *is_percent_hint = (strchr(line, '%') != NULL || strstr(line, "％") != NULL);
        return (strchr(line, '%') || strstr(line, "％")) ? MEC_STAT_ATTACK_PERCENT : MEC_STAT_ATTACK_FLAT;
    }
    if (mec_utf8_contains(line, "生命") || mec_utf8_contains(line, "生命值")) {
        if (is_percent_hint) *is_percent_hint = (strchr(line, '%') != NULL || strstr(line, "％") != NULL);
        return (strchr(line, '%') || strstr(line, "％")) ? MEC_STAT_HP_PERCENT : MEC_STAT_HP_FLAT;
    }
    if (mec_utf8_contains(line, "防御")) {
        if (is_percent_hint) *is_percent_hint = (strchr(line, '%') != NULL || strstr(line, "％") != NULL);
        return (strchr(line, '%') || strstr(line, "％")) ? MEC_STAT_DEF_PERCENT : MEC_STAT_DEF_FLAT;
    }
    if (has_any(line, element_words)) { if (is_percent_hint) *is_percent_hint = 1; return MEC_STAT_ELEMENT_DMG; }

    return MEC_STAT_UNKNOWN;
}
