/* echo_grade.c —— 声骸单件评分定级
   评分 = 60（主词条底座分）+ Σ 有效副词条得分
   副词条得分按所属档位计：档位/满档 × 16（如8档词条每档2分，4档词条每档4分）
   5条全满=140(ACE) 5条中高(6档)=120(SSS) 5条中等(5档)=110(SS) 依此类推
   等级分数线：ACE ≥125 | SSS 120~124 | SS 110~119 | S 100~109
              A 90~99 | B 80~89 | C 70~79 | D 60~69
   刚需双暴的角色：无双暴时最高只能到 C（单暴）/ D（无暴）；
   双暴均为最低档且无其他有效词条时强制为 B。
   请求 data 为紧凑串：声骸之间 '|'，副词条之间 ','，词条名:值（值可带 %）。 */
#include "mec_common.h"
#include "echo_grade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SUBS 8
#define MAX_VALID 16
#define MAX_NAME 64

typedef struct {
    const char* name;
    double tiers[8];
    int n;
} StatTier;

static const StatTier TIER_TABLES[] = {
    { "暴击",          { 6.3,  6.9,  7.5,  8.1,  8.7,  9.3,  9.9, 10.5 }, 8 },
    { "暴击伤害",      {12.6, 13.8, 15.0, 16.2, 17.4, 18.6, 19.8, 21.0 }, 8 },
    { "共鸣技能伤害加成", { 6.4,  7.1,  7.9,  8.6,  9.4, 10.1, 10.9, 11.6 }, 8 },
    { "共鸣解放伤害加成", { 6.4,  7.1,  7.9,  8.6,  9.4, 10.1, 10.9, 11.6 }, 8 },
    { "普攻伤害加成",   { 6.4,  7.1,  7.9,  8.6,  9.4, 10.1, 10.9, 11.6 }, 8 },
    { "重击伤害加成",   { 6.4,  7.1,  7.9,  8.6,  9.4, 10.1, 10.9, 11.6 }, 8 },
    { "攻击百分比",     { 6.4,  7.1,  7.9,  8.6,  9.4, 10.1, 10.9, 11.6 }, 8 },
    { "固定攻击",      {30.0, 40.0, 50.0, 60.0 }, 4 },
    { "防御百分比",     { 8.1,  9.0, 10.0, 10.9, 11.8, 12.8, 13.8, 14.7 }, 8 },
    { "固定防御",      {40.0, 50.0, 60.0, 70.0 }, 4 },
    { "生命百分比",     { 6.4,  7.1,  7.9,  8.6,  9.4, 10.1, 10.9, 11.6 }, 8 },
    { "固定生命",      {320.0, 360.0, 390.0, 430.0, 470.0, 510.0, 540.0, 580.0 }, 8 },
    { "共鸣效率",      { 6.8,  7.6,  8.4,  9.2, 10.0, 10.8, 11.6, 12.4 }, 8 },
};
#define TIER_TABLE_COUNT (int)(sizeof(TIER_TABLES) / sizeof(TIER_TABLES[0]))

typedef struct {
    int requires_double_crit;
    int valid_count;
    char valid[MAX_VALID][MAX_NAME];
} CharRule;

/* 在 rules JSON 中定位 "name" 后的 { ... } 对象（括号配平），返回对象起始或 NULL */
static const char* find_char_object(const char* json, const char* name) {
    char pat[MAX_NAME + 8];
    snprintf(pat, sizeof(pat), "\"%s\"", name);
    const char* p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char* q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (*q == '{') return q;
        }
        p++;
    }
    return NULL;
}

static int load_char_rule(const char* root, const char* character, CharRule* rule) {
    memset(rule, 0, sizeof(*rule));
    char path[MEC_PATH_MAX];
    mec_path_join(path, sizeof(path), root, "resources");
    mec_path_join(path, sizeof(path), path, "echo_grade_rules.json");
    size_t size = 0;
    char* json = mec_read_text_file(path, &size);
    if (!json) {
        mec_set_error("无法读取 %s", path);
        return -1;
    }
    const char* obj = find_char_object(json, character);
    if (!obj) {
        free(json);
        mec_set_error("角色 %s 未配置有效词条", character);
        return 1;   /* 未配置：不算错误 */
    }
    /* 对象范围（括号配平） */
    const char* end = obj;
    int depth = 0;
    while (*end) {
        if (*end == '{') depth++;
        else if (*end == '}') { depth--; if (depth == 0) break; }
        end++;
    }
    /* 双暴需求（默认 true；显式 false 才关闭） */
    rule->requires_double_crit = 1;
    {
        const char* f = strstr(obj, "\"requiresDoubleCrit\"");
        if (f && f < end) {
            const char* v = strchr(f + 20, ':');
            v++;
            while (*v == ' ') v++;
            if (strncmp(v, "false", 5) == 0) rule->requires_double_crit = 0;
        }
    }
    /* validSubs 字符串数组 */
    const char* arr = strstr(obj, "\"validSubs\"");
    if (arr && arr < end) arr = strchr(arr, '[');
    if (arr && arr < end) {
        arr++;
        while (*arr && *arr != ']' && rule->valid_count < MAX_VALID) {
            if (*arr == '"') {
                arr++;
                char* dst = rule->valid[rule->valid_count];
                int n = 0;
                while (*arr && *arr != '"' && n < MAX_NAME - 1) dst[n++] = *arr++;
                dst[n] = 0;
                if (*arr == '"') arr++;
                rule->valid_count++;
            } else {
                arr++;
            }
        }
    }
    free(json);
    return 0;
}

static int is_valid_sub(const CharRule* rule, const char* canon) {
    for (int i = 0; i < rule->valid_count; ++i)
        if (strcmp(rule->valid[i], canon) == 0) return 1;
    return 0;
}

/* UI 词条名 + 值字符串 → 规范名 + 数值（百分比按是否带 '%' 区分固定/百分比） */
static int normalize_sub(const char* name, const char* value, char* canon, double* val) {
    char buf[MAX_NAME];
    snprintf(buf, sizeof(buf), "%s", value);
    char* pct = strchr(buf, '%');
    if (pct) *pct = 0;
    *val = atof(buf);
    if (strcmp(name, "暴击") == 0) snprintf(canon, MAX_NAME, "暴击");
    else if (strcmp(name, "暴击伤害") == 0) snprintf(canon, MAX_NAME, "暴击伤害");
    else if (strcmp(name, "共鸣效率") == 0) snprintf(canon, MAX_NAME, "共鸣效率");
    else if (strcmp(name, "共鸣技能伤害加成") == 0) snprintf(canon, MAX_NAME, "共鸣技能伤害加成");
    else if (strcmp(name, "共鸣解放伤害加成") == 0) snprintf(canon, MAX_NAME, "共鸣解放伤害加成");
    else if (strcmp(name, "普攻伤害加成") == 0) snprintf(canon, MAX_NAME, "普攻伤害加成");
    else if (strcmp(name, "重击伤害加成") == 0) snprintf(canon, MAX_NAME, "重击伤害加成");
    else if (strcmp(name, "攻击") == 0) snprintf(canon, MAX_NAME, pct ? "攻击百分比" : "固定攻击");
    else if (strcmp(name, "攻击百分比") == 0 || strcmp(name, "固定攻击") == 0) snprintf(canon, MAX_NAME, "%s", name);
    else if (strcmp(name, "防御百分比") == 0 || strcmp(name, "固定防御") == 0) snprintf(canon, MAX_NAME, "%s", name);
    else if (strcmp(name, "生命百分比") == 0 || strcmp(name, "固定生命") == 0) snprintf(canon, MAX_NAME, "%s", name);
    else if (strcmp(name, "防御") == 0) snprintf(canon, MAX_NAME, pct ? "防御百分比" : "固定防御");
    else if (strcmp(name, "生命") == 0) snprintf(canon, MAX_NAME, pct ? "生命百分比" : "固定生命");
    else return -1;
    return 0;
}

typedef struct {
    double points;      /* 展示用得分 */
    double ratio_sum;   /* 有效词条档位比合计（(档位/满档) 之和） */
    int count;          /* 有效词条数量 */
    int crit;           /* bit0=有暴击 bit1=有暴击伤害 */
    int min_crit;       /* 双暴均为最低档 */
    int effective_others;
} EchoScore;

static void score_one_sub(const CharRule* rule, const char* tok, EchoScore* es,
                          int* crit_seen, int* crit_min) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", tok);
    char* colon = strchr(buf, ':');
    if (!colon) return;
    *colon = 0;
    const char* sval = colon + 1;
    char canon[MAX_NAME];
    double val = 0;
    if (normalize_sub(buf, sval, canon, &val) != 0) return;
    if (val <= 0) return;
    const StatTier* table = NULL;
    for (int i = 0; i < TIER_TABLE_COUNT; ++i)
        if (strcmp(TIER_TABLES[i].name, canon) == 0) { table = &TIER_TABLES[i]; break; }
    if (!table) return;
    int crit_idx = strcmp(canon, "暴击") == 0 ? 0 : (strcmp(canon, "暴击伤害") == 0 ? 1 : -1);
    if (!is_valid_sub(rule, canon)) return;
    int tier = 1;                       /* 低于最低档按第1档计 */
    for (int i = 0; i < table->n; ++i)
        if (val >= table->tiers[i]) tier = i + 1;
    es->ratio_sum += (double)tier / table->n;
    es->count++;
    es->points += 8.0 + (double)tier / table->n * 8.0;
    if (crit_idx >= 0) {
        es->crit |= (1 << crit_idx);
        crit_seen[crit_idx] = 1;
        crit_min[crit_idx] = (val <= table->tiers[0]) ? 1 : 0;
    } else {
        es->effective_others++;
    }
}

/* 对一段副词条串（逗号分隔）评分。手写分段，避免 strtok 嵌套破坏外层状态 */
static void score_echo(const CharRule* rule, const char* subs, EchoScore* es) {
    memset(es, 0, sizeof(*es));
    int crit_seen[2] = { 0, 0 };
    int crit_min[2] = { 1, 1 };
    const char* p = subs;
    while (p && *p) {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0) {
            char tok[128];
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, p, len);
            tok[len] = 0;
            score_one_sub(rule, tok, es, crit_seen, crit_min);
        }
        p = comma ? comma + 1 : NULL;
    }
    if (crit_seen[0] && crit_seen[1]) {
        es->crit = 3;
        es->min_crit = (crit_min[0] && crit_min[1]);
    }
}

/* 等级序：D=0 < C=1 < B=2 < A=3 < S=4 < SS=5 < SSS=6 < ACE=7 */
static int grade_rank(const char* g) {
    if (strcmp(g, "ACE") == 0) return 7;
    if (strcmp(g, "SSS") == 0) return 6;
    if (strcmp(g, "SS") == 0)  return 5;
    if (strcmp(g, "S") == 0)   return 4;
    if (strcmp(g, "A") == 0)   return 3;
    if (strcmp(g, "B") == 0)   return 2;
    if (strcmp(g, "C") == 0)   return 1;
    return 0;
}

/* 按有效词条数量与平均档位取级（刚需双暴的角色仅在已有双暴时调用） */
static const char* grade_from_count(int count, double avg) {
    if (count >= 5) {
        return avg >= 0.75 ? "ACE" : avg >= 0.50 ? "SSS" : avg >= 0.30 ? "SS" : avg >= 0.18 ? "S" : "A";
    }
    if (count == 4) return avg >= 0.50 ? "SS" : avg >= 0.25 ? "S" : "A";
    if (count == 3) return avg >= 0.50 ? "S" : avg >= 0.33 ? "A" : "B";
    if (count == 2) return avg >= 0.50 ? "A" : "B";
    return "C";
}

int mec_echo_grade_json(const char* root, const char* character, const char* data, char* out, int out_size) {
    if (!root || !character || !data || !out || out_size <= 0) return -1;
    CharRule rule;
    int rc = load_char_rule(root, character, &rule);
    if (rc != 0) {
        if (rc == 1) {   /* 未配置：返回空结果，前端不显示标识 */
            snprintf(out, out_size, "{\"ok\":true,\"found\":false,\"results\":[]}");
            return 0;
        }
        return -1;
    }

    int pos = 0;
    mec_json_append(out, out_size, &pos, "{\"ok\":true,\"found\":true,\"results\":[");
    const char* p = data;
    int first = 1;
    while (p && *p) {
        const char* pipe = strchr(p, '|');
        size_t len = pipe ? (size_t)(pipe - p) : strlen(p);
        char seg[1024];
        if (len >= sizeof(seg)) len = sizeof(seg) - 1;
        memcpy(seg, p, len);
        seg[len] = 0;
        EchoScore es;
        score_echo(&rule, seg, &es);
        /* 等级 = f(有效词条数量, 平均档位)；分数仅用于展示，随后对齐到等级区间 */
        double avg = es.count > 0 ? es.ratio_sum / es.count : 0.0;
        const char* grade;
        if (rule.requires_double_crit && (es.crit & 3) == 0) {
            grade = "D";                                /* 刚需双暴但无暴击：D */
        } else if (rule.requires_double_crit && (es.crit & 3) != 3) {
            /* 刚需双暴但无双暴：按数量表定级后封顶——单暴+2~3个其他有效可到 B~A，纯单暴 C，无暴 D */
            if ((es.crit & 3) == 0) {
                grade = "D";
            } else {
                int others = es.count - 1;          /* 除单暴外的有效词条数 */
                if (others <= 0) grade = "C";       /* 纯单暴：C */
                else if (others == 1) grade = "B";  /* 单暴+1有效：B */
                else {                              /* 单暴+2~3有效：B~A（封顶 A） */
                    grade = grade_from_count(es.count, avg);
                    if (grade_rank(grade) > grade_rank("A")) grade = "A";
                }
            }
        } else if (es.count == 0) {
            grade = "D";
        } else {
            grade = grade_from_count(es.count, avg);
        }
        /* 展示分数对齐到等级区间 */
        double lo = 60.0, hi = 69.9;
        if      (strcmp(grade, "ACE") == 0) { lo = 125.0; hi = 140.0; }
        else if (strcmp(grade, "SSS") == 0) { lo = 120.0; hi = 124.9; }
        else if (strcmp(grade, "SS")  == 0) { lo = 110.0; hi = 119.9; }
        else if (strcmp(grade, "S")   == 0) { lo = 100.0; hi = 109.9; }
        else if (strcmp(grade, "A")   == 0) { lo = 90.0;  hi = 99.9;  }
        else if (strcmp(grade, "B")   == 0) { lo = 80.0;  hi = 89.9;  }
        else if (strcmp(grade, "C")   == 0) { lo = 70.0;  hi = 79.9;  }
        double score = 60.0 + es.points;
        if (score < lo) score = lo;
        if (score > hi) score = hi;
        mec_json_append(out, out_size, &pos, "%s{\"score\":%.1f,\"grade\":\"%s\"}",
                        first ? "" : ",", score, grade);
        first = 0;
        p = pipe ? pipe + 1 : NULL;
    }
    mec_json_append(out, out_size, &pos, "]}");
    return 0;
}
