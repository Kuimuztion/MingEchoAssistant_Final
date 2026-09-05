#ifndef MEC_COMMON_H
#define MEC_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include "mec_api.h"

#ifndef MEC_PATH_MAX
#define MEC_PATH_MAX 1024
#endif

#ifdef _WIN32
#define MEC_SEP "\\"
#else
#define MEC_SEP "/"
#endif

typedef struct MecContext {
    char root[MEC_PATH_MAX];
    char database_dir[MEC_PATH_MAX];
    char models_dir[MEC_PATH_MAX];
    char last_error[512];
    int db_loaded;
} MecContext;

extern MecContext g_mec;

void mec_set_error(const char* fmt, ...);
void mec_path_join(char* out, size_t out_size, const char* a, const char* b);
int mec_file_exists(const char* path);
char* mec_read_text_file(const char* path, size_t* out_size);
int mec_write_json_error(char* out, int out_size, const char* message);
int mec_json_append(char* out, int out_size, int* pos, const char* fmt, ...);
void mec_trim(char* s);
float mec_clampf(float v, float lo, float hi);
int mec_utf8_contains(const char* haystack, const char* needle);

/* database */
int mec_db_load(const char* root_dir);
int mec_db_find_character(const char* name, MecCharacterProfile* out_profile);
int mec_csv_load_profile(const char* csv_path, const char* name, MecCharacterProfile* out);
int mec_db_count_characters(void);
int mec_db_count_echoes(void);
int mec_db_count_weapons(void);
int mec_db_count_resonance(void);
int mec_db_count_resonance_echoes(void);

/* parser */
const char* mec_stat_type_name(MecStatType type);
MecStatType mec_detect_stat_type(const char* line, int* is_percent_hint);
float mec_parse_first_number(const char* line, int* found, int* is_percent);
void mec_panel_from_stats(const MecStat* stats, int count, MecPanel* panel);

/* scoring */
float mec_score_stats_for_character(const MecCharacterProfile* profile, const MecStat* echo_stats, int echo_count, const MecPanel* panel, char* reason, int reason_size);
void mec_build_recommendation(const MecCharacterProfile* profile, const MecPanel* panel, char* out, int out_size);

#endif
