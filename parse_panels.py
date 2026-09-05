#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""解析 resources/Role/角色面板.md → database/character_panels.json + web/character_panels.json
支持：小毕业/大毕业核心属性、伤害加成范围、有效词条目标、毕业(无大小区分)、无需求属性"""
import re, json, os

ROOT = os.path.dirname(os.path.abspath(__file__))
MD_PATH = os.path.join(ROOT, "resources", "Role", "角色面板.md")
OUT_PATH = os.path.join(ROOT, "database", "character_panels.json")
WEB_PATH = os.path.join(ROOT, "web", "character_panels.json")

def parse_stats_line(text):
    """解析一行毕业属性，返回 (core_stats, damage_bonuses)
    core_stats: {属性名: {value, unit, raw}}
    damage_bonuses: [{name, min, max, unit, raw}]
    """
    core_stats = {}
    damage_bonuses = []

    # 预处理：去掉括号内容（如"(无专武)"、"(有专武)"、"(舒适值约140%；不同体系会浮动)"）
    text = re.sub(r'[（(][^）)]*[）)]', '', text)
    # 预处理：去掉斜杠分隔的备选值（如" / ≥ 40000"），只保留第一个值
    text = re.sub(r'\s*/\s*[≥>=]+\s*[\d.]+\s*%?', '', text)
    # 预处理：统一术语"伤害面板"→"伤害加成"（原文笔误）
    text = text.replace('伤害面板', '伤害加成')

    # 按分号分割：核心属性；伤害加成范围；伤害加成范围...
    parts = re.split(r'[；;]', text)

    for part in parts:
        part = part.strip().rstrip('。').strip()
        if not part:
            continue

        # 1. 解析核心属性：属性名 ≥ 数值%? （用逗号/顿号分隔多个）
        core_matches = re.findall(r'([^,，、]+?)\s*[≥>=]+\s*([\d.]+)\s*(%?)', part)
        for key, val, unit in core_matches:
            key = key.strip()
            # 跳过"无需求"的属性
            if '无需求' in key or '不需要' in key:
                continue
            # 跳过明显不是属性名的（如包含"约"、"~"、"/"）
            if '~' in key or '约' in key or '/' in key:
                continue
            # 统一属性名：生命值→生命，暴击率→暴击
            key = key.replace('生命值', '生命').replace('暴击率', '暴击')
            if key and float(val) > 0:
                core_stats[key] = {"value": float(val), "unit": unit, "raw": f"{key} ≥ {val}{unit}"}

        # 1b. 特殊格式："暴击率建议接近/达到 100%"（达妮娅）
        special_matches = re.findall(r'([^,，、]+?)\s*建议接近/达到\s*([\d.]+)\s*(%?)', part)
        for key, val, unit in special_matches:
            key = key.strip().replace('生命值', '生命').replace('暴击率', '暴击')
            if key and float(val) > 0 and key not in core_stats:
                core_stats[key] = {"value": float(val), "unit": unit, "raw": f"{key} ≥ {val}{unit}"}

        # 2. 解析伤害加成范围：属性名 约? 数值%~数值%
        bonus_matches = re.findall(r'([^,，、；;]+?)\s*约?\s*([\d.]+)\s*%?\s*~\s*([\d.]+)\s*%?', part)
        for name, min_val, max_val in bonus_matches:
            name = name.strip()
            # 清理名称末尾的"约"、"≥"、">"等符号
            name = re.sub(r'[约≥>=]+\s*$', '', name).strip()
            # 统一属性名
            name = name.replace('生命值', '生命').replace('暴击率', '暴击')
            if name and float(min_val) > 0:
                damage_bonuses.append({
                    "name": name,
                    "min": float(min_val),
                    "max": float(max_val),
                    "unit": "%",
                    "raw": f"{name} {min_val}%~{max_val}%"
                })

    return core_stats, damage_bonuses


def parse_valid_keywords(text):
    """解析'有效词条目标：暴击、暴击伤害、共鸣解放伤害加成、攻击百分比'"""
    if not text or not text.strip():
        return []
    # 按逗号/顿号/分号分割
    items = re.split(r'[,，、；;]', text)
    return [item.strip() for item in items if item.strip()]


def parse_md(text):
    panels = {}
    # 按 "# 角色名" 分段
    sections = re.split(r'\n(?=# )', text)
    for sec in sections:
        lines = sec.strip().split('\n')
        if not lines or not lines[0].startswith('# '):
            continue
        name = lines[0][2:].strip()
        # 跳过非角色条目（如文件头、核验来源）
        if name in ('角色面板（修订版）', '核验来源（本版）'):
            continue

        panel = {"name": name, "raw": sec.strip()}

        for line in lines[1:]:
            line = line.strip()
            if not line:
                continue

            # 角色/定位
            m = re.match(r'角色：(.+?)\s+定位：(.+)', line)
            if m:
                panel["character"] = m.group(1).strip()
                panel["role"] = m.group(2).strip()
                em = re.search(r'\(([^/]+)', panel["role"])
                if em:
                    panel["element"] = em.group(1).strip()
                continue

            # 小毕业
            if line.startswith('小毕业：'):
                core, bonuses = parse_stats_line(line[len('小毕业：'):])
                panel["small_graduate"] = core
                if bonuses:
                    panel["small_damage_bonuses"] = bonuses
                continue

            # 大毕业
            if line.startswith('大毕业：'):
                core, bonuses = parse_stats_line(line[len('大毕业：'):])
                panel["big_graduate"] = core
                if bonuses:
                    panel["big_damage_bonuses"] = bonuses
                continue

            # 毕业（无大小区分，如穗穗）
            if line.startswith('毕业：') or line.startswith('毕业参考：'):
                prefix = '毕业参考：' if line.startswith('毕业参考：') else '毕业：'
                core, bonuses = parse_stats_line(line[len(prefix):])
                panel["small_graduate"] = core
                panel["big_graduate"] = core  # 没有大毕业，用同一套
                if bonuses:
                    panel["small_damage_bonuses"] = bonuses
                    panel["big_damage_bonuses"] = bonuses
                continue

            # 优先级
            if line.startswith('优先级：'):
                panel["priority"] = line[len('优先级：'):].strip()
                continue

            # 有效词条目标
            if line.startswith('有效词条目标：'):
                panel["valid_keywords"] = parse_valid_keywords(line[len('有效词条目标：'):])
                continue

            # 武器
            if line.startswith('武器：'):
                weapons = re.split(r'[>、]', line[len('武器：'):])
                panel["weapons"] = [w.strip() for w in weapons if w.strip()]
                continue

            # 声骸
            if line.startswith('声骸：'):
                panel["echo_set"] = line[len('声骸：'):].strip()
                continue

            # COST4/3/1
            if line.startswith('COST4：'):
                panel["cost4"] = line[len('COST4：'):].strip()
                continue
            if line.startswith('COST3：'):
                panel["cost3"] = line[len('COST3：'):].strip()
                continue
            if line.startswith('COST1：'):
                panel["cost1"] = line[len('COST1：'):].strip()
                continue

        # 只保留有小毕业数据的角色（过滤掉文件头等非角色条目）
        if panel.get('small_graduate'):
            panels[name] = panel

    return panels


if __name__ == '__main__':
    with open(MD_PATH, 'r', encoding='utf-8') as f:
        text = f.read()
    if text.startswith('\ufeff'):
        text = text[1:]

    panels = parse_md(text)

    # 输出到 database/
    with open(OUT_PATH, 'w', encoding='utf-8') as f:
        json.dump(panels, f, ensure_ascii=False, indent=2)

    # 同步到 web/
    with open(WEB_PATH, 'w', encoding='utf-8') as f:
        json.dump(panels, f, ensure_ascii=False, indent=2)

    print(f"解析完成：{len(panels)} 个角色")
    print(f"  → {OUT_PATH}")
    print(f"  → {WEB_PATH}")

    # 打印几个角色的解析结果供验证
    for test_name in ['爱弥斯', '维里奈', '秧秧·玄翎', '守岸人']:
        if test_name in panels:
            p = panels[test_name]
            print(f"\n=== {test_name} ===")
            print(f"  小毕业核心属性: {list(p.get('small_graduate', {}).keys())}")
            print(f"  小毕业伤害加成: {[b['name'] + ' ' + str(b['min']) + '~' + str(b['max']) + '%' for b in p.get('small_damage_bonuses', [])]}")
            print(f"  有效词条目标: {p.get('valid_keywords', [])}")
            print(f"  武器: {p.get('weapons', [])[:3]}")
