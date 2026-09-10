/* 从 web/team_build.html 的养成模版生成 resources/echo_grade_rules.json
   （每角色的有效副词条 + 是否刚需双暴）。重复运行覆盖。 */
const fs = require('fs');
const html = fs.readFileSync('web/team_build.html', 'utf8');

const MAP = {
  '攻击': ['攻击百分比', '固定攻击'],
  '暴击': ['暴击'],
  '暴击伤害': ['暴击伤害'],
  '共鸣效率': ['共鸣效率'],
  '共鸣解放': ['共鸣解放伤害加成'],
  '共鸣技能': ['共鸣技能伤害加成'],
  '普攻伤害': ['普攻伤害加成'],
  '重击伤害': ['重击伤害加成'],
  '生命': ['生命百分比', '固定生命'],
  '防御': ['防御百分比', '固定防御'],
};

const rules = {};
const re = /row\('([^']+)',\s*\[\[([\s\S]*?)\]\]/g;
let m;
while ((m = re.exec(html)) !== null) {
  const name = m[1];
  const keys = [...m[2].matchAll(/'([^']+)'(?=\s*,\s*')/g)].map(x => x[1]);
  const set = rules[name] || (rules[name] = new Set());
  for (const k of keys) {
    const canon = MAP[k.replace(/（.*?）/g, '').trim()];
    if (canon) canon.forEach(x => set.add(x));
    else console.log('[未知词条]', name, k);
  }
}
const out = { comment: '声骸评分有效词条配置：validSubs=有效副词条(未列出的词条不计分)；requiresDoubleCrit=该角色是否刚需双暴(由模板是否同时包含暴击与暴击伤害自动推导，可手动覆盖)。新增角色照此格式添加即可。', characters: {} };
for (const [name, set] of Object.entries(rules)) {
  const subs = [...set];
  out.characters[name] = {
    requiresDoubleCrit: subs.includes('暴击') && subs.includes('暴击伤害'),
    validSubs: subs,
  };
}
fs.writeFileSync('resources/echo_grade_rules.json', JSON.stringify(out, null, 2), 'utf8');
console.log('generated', Object.keys(out.characters).length, 'characters');
