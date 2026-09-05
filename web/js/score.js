/* score.js —— 声骸评分页 */
const Score = {
  characters: [],
  selected: '',
  targets: null,

  async init() {
    document.getElementById('scoreBtn').addEventListener('click', () => this.score());
    document.getElementById('recommendBtn').addEventListener('click', () => this.recommend());
    this.setupSelect();

    const data = await Api.json('/api/characters');
    if (data.ok) {
      this.characters = data.characters || [];
      this.renderList();
      if (this.characters.length) this.select(this.characters[0]);
    }
    this.loadTargets();
  },

  /* 目标面板数据（一次拉取，全角色） */
  async loadTargets() {
    if (this.targets) return this.targets;
    try {
      const data = await Api.json('/api/targets');
      if (data.ok && data.characters) this.targets = data.characters;
    } catch (e) { /* 忽略，回退到 recommend */ }
    return this.targets;
  },

  /* ---------- 自绘角色下拉（可搜索、限高滚动） ---------- */
  setupSelect() {
    const head = document.getElementById('charSelectHead');
    const drop = document.getElementById('charSelectDrop');
    const search = document.getElementById('charSearch');
    head.addEventListener('click', (e) => {
      e.stopPropagation();
      const open = !drop.classList.contains('open');
      document.querySelectorAll('.cselect-drop.open').forEach((d) => { if (d !== drop) d.classList.remove('open'); });
      drop.classList.toggle('open', open);
      if (open) { search.value = ''; this.renderList(); search.focus(); }
    });
    search.addEventListener('input', () => this.renderList());
    search.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        const opt = drop.querySelector('.cselect-opt');
        if (opt) { opt.click(); }
        e.preventDefault();
      }
    });
    document.addEventListener('click', (e) => {
      if (!document.getElementById('charSelectBox').contains(e.target)) drop.classList.remove('open');
    });
  },

  renderList() {
    const list = document.getElementById('charList');
    const q = (document.getElementById('charSearch').value || '').trim();
    const matched = q ? this.characters.filter((n) => n.toLowerCase().includes(q.toLowerCase())) : this.characters;
    if (!matched.length) { list.innerHTML = '<div class="cselect-empty">无匹配角色</div>'; return; }
    list.innerHTML = matched.map((n) =>
      '<button type="button" class="cselect-opt' + (n === this.selected ? ' active' : '') + '" data-name="' +
      escapeHtml(n).replace(/"/g, '&quot;') + '">' + escapeHtml(n) + '</button>'
    ).join('');
    list.querySelectorAll('.cselect-opt').forEach((b) => {
      b.addEventListener('click', () => { this.select(b.dataset.name); });
    });
  },

  select(name) {
    this.selected = name;
    document.getElementById('charSelectVal').textContent = name;
    document.getElementById('charSelectDrop').classList.remove('open');
    this.onCharacterChange();
  },

  current() {
    return this.selected || (this.characters[0] || '今汐');
  },

  onCharacterChange() {
    const name = this.current();
    document.getElementById('portraitName').textContent = name;
    const img = document.getElementById('portraitImg');
    img.onerror = () => { img.style.display = 'none'; };
    img.onload = () => { img.style.display = ''; };
    img.src = '/res/Project/角色/' + encodeURIComponent(name) + '.webp';
    // 目标面板（调用 recommend 接口获取目标）
    this.loadTarget(name);
  },

  async loadTarget(name) {
    const box = document.getElementById('targetText');
    await this.loadTargets();
    const t = this.targets && this.targets[name];
    if (t) {
      box.innerHTML = this.renderTarget(name, t);
      return;
    }
    // 兜底：无目标数据时回退到 recommend（兼容）
    const panel = document.getElementById('panelText').value.trim();
    const data = await Api.json('/api/recommend', { character: name, panel_text: panel });
    if (data.ok && data.recommendation) {
      box.innerHTML = '<div class="target-line">' + escapeHtml(normNewline(data.recommendation)) + '</div>';
    } else {
      box.textContent = '（暂无目标数据）';
    }
  },

  /* 把目标面板渲染成 Markdown 风格表格（阶段 × 属性阈值） */
  renderTarget(name, t) {
    const statNames = { crit: '暴击率', crit_damage: '暴击伤害', attack: '攻击力', energy: '共鸣效率', hp: '生命值', def: '防御' };
    const order = (t.statOrder && t.statOrder.length) ? t.statOrder
      : ['crit', 'crit_damage', 'attack', 'energy', 'hp', 'def'].filter((k) => t.stages && t.stages.some((s) => s.cells && s.cells[k]));
    let html = '';
    html += '<div class="target-line">角色：' + escapeHtml(name) + '｜定位：' + escapeHtml(t.role || '') + '</div>';
    if (t.desc && t.desc !== t.role) html += '<div class="target-desc">' + escapeHtml(t.desc) + '</div>';
    if (t.stages && t.stages.length) {
      html += '<table class="target-table"><thead><tr><th>阶段</th>';
      order.forEach((k) => { html += '<th>' + escapeHtml(statNames[k] || k) + '</th>'; });
      html += '</tr></thead><tbody>';
      t.stages.forEach((s) => {
        html += '<tr><td class="t-stage">' + escapeHtml(s.label) + '</td>';
        order.forEach((k) => {
          const v = (s.cells && s.cells[k]) || '—';
          html += '<td>' + escapeHtml(v) + '</td>';
        });
        html += '</tr>';
      });
      html += '</tbody></table>';
    }
    if (t.priority) html += '<div class="target-prio">优先级：' + escapeHtml(t.priority) + '</div>';
    if (t.weapon || t.echoes || t.cost) {
      html += '<div class="target-gear">';
      if (t.weapon) html += '<div><span class="gear-key">武器</span>' + escapeHtml(t.weapon) + '</div>';
      if (t.echoes) html += '<div><span class="gear-key">声骸</span>' + escapeHtml(t.echoes) + '</div>';
      if (t.cost) html += '<div class="gear-cost">' + escapeHtml(t.cost) + '</div>';
      html += '</div>';
    }
    return html;
  },

  async score() {
    const character = this.current();
    const panel_text = document.getElementById('panelText').value.trim();
    const echo_text = document.getElementById('echoText').value.trim();
    if (!panel_text && !echo_text) { this.showResult({ error: '请至少填写角色主面板（或单个声骸词条）' }); return; }
    document.getElementById('scoreBusy').classList.remove('hidden');
    const data = await Api.json('/api/score', { character, echo_text, panel_text });
    document.getElementById('scoreBusy').classList.add('hidden');
    this.showResult(data);
  },

  /* 养成推荐：调用 Agnes AI 流式生成个性化建议 */
  recommend() {
    const character = this.current();
    const panel_text = document.getElementById('panelText').value.trim();
    const echo_text = document.getElementById('echoText').value.trim();
    if (!panel_text && !echo_text) { this.showResult({ error: '请至少填写角色主面板' }); return; }

    const box = document.getElementById('scoreResult');
    box.innerHTML = '<div class="result-card"><div class="result-top"><span class="card-hint">Agnes AI 养成建议</span><span class="spacer"></span><span id="recBusy" class="card-hint">生成中…</span></div><div id="recText" class="rec-text"></div></div>';
    const recText = document.getElementById('recText');
    const recBusy = document.getElementById('recBusy');

    const t = (this.targets && this.targets[character]) || {};
    const bigStage = (t.stages && t.stages.find((s) => s.label === '大毕业')) || (t.stages && t.stages[t.stages.length - 1]) || { cells: {} };
    const targetLine = Object.entries(bigStage.cells || {}).map(([k, v]) => {
      const names = { crit: '暴击', crit_damage: '暴伤', attack: '攻击', energy: '共鸣效率', hp: '生命', def: '防御' };
      return (names[k] || k) + v;
    }).join(' / ');

    const messages = [
      { role: 'system', content: '你是鸣潮（Wuthering Waves）的资深配装养成顾问。请用中文给玩家一段简洁、有针对性的养成建议（200字以内），直接用自然语言分点说明，不要用Markdown表格。语气专业但友好。' },
      { role: 'user', content:
        '角色：' + character + (t.role ? '（' + t.role + '）' : '') + '\n' +
        '目标面板（' + (bigStage.label || '毕业线') + '）：' + (targetLine || '参考通用毕业线') + '\n' +
        (t.priority ? '属性优先级：' + t.priority + '\n' : '') +
        '当前面板：' + (panel_text || '（未填）') + '\n' +
        (echo_text ? '单个声骸词条：' + echo_text + '\n' : '') +
        '请告诉玩家：当前面板距离目标还有哪些差距、优先补什么、有什么注意事项。'
      }
    ];

    if (this._recStream) this._recStream.cancel();
    this._recStream = Api.stream('/api/chat', { messages, stream: true },
      (d) => { recText.textContent = normNewline(d.fullContent || ''); },
      () => { recBusy.textContent = '完成'; recBusy.style.color = 'var(--gold)'; },
      (err) => { recBusy.textContent = '出错：' + String(err).slice(0, 60); recBusy.style.color = '#e06060'; }
    );
  },

  showResult(data) {
    const box = document.getElementById('scoreResult');
    if (!data.ok) {
      box.innerHTML = '<div class="error-text">' + escapeHtml(data.error || '计算失败') + '</div>';
      return;
    }
    let html = '<div class="result-card">';
    html += '<div class="result-top">';
    if (data.grade) html += '<div class="grade-badge">' + escapeHtml(data.grade) + '</div>';
    if (typeof data.score === 'number') html += '<div class="score-num">' + data.score.toFixed(1) + ' / 100</div>';
    if (data.role) html += '<span class="card-hint">' + escapeHtml(data.role) + '</span>';
    html += '</div>';
    if (data.reason) html += '<div class="reason-text">' + escapeHtml(normNewline(data.reason)) + '</div>';
    if (data.recommendation) html += '<div class="rec-text">' + escapeHtml(normNewline(data.recommendation)) + '</div>';
    if (data.parsed_stats && data.parsed_stats.length) {
      html += '<div class="stat-chips">';
      data.parsed_stats.forEach((s) => {
        html += '<span class="stat-chip">' + escapeHtml(s.name) + ' ' + s.value + (s.percent ? '%' : '') + '</span>';
      });
      html += '</div>';
    }
    html += '</div>';
    box.innerHTML = html;
  },
};

function escapeHtml(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
