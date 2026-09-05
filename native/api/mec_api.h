#ifndef MEC_API_H
#define MEC_API_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MEC_STATIC
  #define MEC_EXPORT
#elif defined(_WIN32)
  #ifdef MEC_BUILD_DLL
    #define MEC_EXPORT __declspec(dllexport)
  #else
    #define MEC_EXPORT __declspec(dllimport)
  #endif
#else
  #define MEC_EXPORT
#endif

#define MEC_OK 0
#define MEC_ERR -1
#define MEC_ERR_NOT_READY -2
#define MEC_ERR_BAD_ARG -3
#define MEC_ERR_NO_MODEL -4
#define MEC_ERR_BUFFER_SMALL -5

#define MEC_MAX_STAT_NAME 64
#define MEC_MAX_STATS 64
#define MEC_MAX_TEXT 8192
#define MEC_MAX_JSON 16384

typedef enum MecStatType {
    MEC_STAT_UNKNOWN = 0,
    MEC_STAT_CRIT_RATE,
    MEC_STAT_CRIT_DAMAGE,
    MEC_STAT_ATTACK_FLAT,
    MEC_STAT_ATTACK_PERCENT,
    MEC_STAT_HP_FLAT,
    MEC_STAT_HP_PERCENT,
    MEC_STAT_DEF_FLAT,
    MEC_STAT_DEF_PERCENT,
    MEC_STAT_ENERGY_REGEN,
    MEC_STAT_BASIC_DMG,
    MEC_STAT_HEAVY_DMG,
    MEC_STAT_SKILL_DMG,
    MEC_STAT_LIBERATION_DMG,
    MEC_STAT_ELEMENT_DMG,
    MEC_STAT_HEALING_BONUS
} MecStatType;

typedef struct MecStat {
    char name[MEC_MAX_STAT_NAME];
    MecStatType type;
    float value;
    int is_percent;
} MecStat;

typedef struct MecPanel {
    float crit_rate;
    float crit_damage;
    float attack;
    float hp;
    float defense;
    float energy_regen;
    float element_damage;
    float skill_damage;
    float liberation_damage;
    float basic_damage;
    float heavy_damage;
} MecPanel;

typedef struct MecCharacterProfile {
    char name[64];
    char role[32];
    float target_crit_rate;
    float target_crit_damage;
    float target_attack;
    float target_energy;
    float weight_basic;
    float weight_heavy;
    float weight_skill;
    float weight_liberation;
    float weight_attack;
} MecCharacterProfile;

MEC_EXPORT const char* mec_version(void);
MEC_EXPORT int mec_init(const char* root_dir);
MEC_EXPORT void mec_shutdown(void);
MEC_EXPORT const char* mec_last_error(void);

MEC_EXPORT int mec_parse_stats(const char* text, MecStat* out_stats, int max_stats);
MEC_EXPORT int mec_parse_stats_json(const char* text, char* out_json, int out_size);
MEC_EXPORT int mec_score_echo_json(const char* character_name, const char* echo_stats_text, const char* current_panel_text, char* out_json, int out_size);
MEC_EXPORT int mec_recommend_json(const char* character_name, const char* current_panel_text, char* out_json, int out_size);

#ifdef __cplusplus
}
#endif

#endif
