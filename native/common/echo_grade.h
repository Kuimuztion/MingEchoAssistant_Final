#ifndef ECHO_GRADE_H
#define ECHO_GRADE_H

/* 声骸单件评分定级（ACE/SSS/SS/S/A/B/C/D）
   data 为紧凑串：声骸之间用 '|' 分隔，副词条之间用 ',' 分隔，
   词条名与值用 ':' 分隔（值可带 '%'），例如：
   "暴击:6.9%,暴击伤害:21.0%|共鸣效率:10.8%,攻击:6.4%"
   有效词条与双暴需求来自 resources/echo_grade_rules.json */
int mec_echo_grade_json(const char* root, const char* character, const char* data, char* out, int out_size);

#endif
