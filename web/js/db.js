/* db.js —— 数据库页 */
const Db = {
  data: null,
  currentTab: 'echoes',

  async init() {
    document.querySelectorAll('.tab').forEach((tab) => {
      tab.addEventListener('click', () => this.switchTab(tab.dataset.tab));
    });
    const data = await Api.json('/api/database');
    if (!data.ok) {
      document.getElementById('dbList').innerHTML = '<div class="error-text">加载失败：' + escapeHtml(data.error) + '</div>';
      return;
    }
    this.data = data;
    const c = data.counts || {};
    document.getElementById('cCharacters').textContent = c.characters;
    document.getElementById('cEchoes').textContent = c.echoes;
    document.getElementById('cWeapons').textContent = c.weapons;
    document.getElementById('cResonance').textContent = c.resonance;
    document.getElementById('cResEchoes').textContent = c.resonance_echoes;
    this.render();
  },

  switchTab(tab) {
    this.currentTab = tab;
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.dataset.tab === tab));
    this.render();
  },

  /* resources/... -> /res/... */
  resUrl(p) {
    if (!p) return '';
    if (p.startsWith('resources/')) return '/res/' + p.slice('resources/'.length);
    return p;
  },

  render() {
    if (!this.data) return;
    const list = document.getElementById('dbList');
    let items = [];
    if (this.currentTab === 'echoes') {
      items = (this.data.echoes || []).map((e) =>
        '<div class="db-card">' +
        '<img class="db-icon" src="' + this.resUrl(e.image) + '" alt="" onerror="this.style.visibility=\'hidden\'" />' +
        '<span class="db-name">' + escapeHtml(e.name) + '</span>' +
        '<span class="db-meta">COST ' + e.cost + ' · ' + escapeHtml(e.level || '') + '</span>' +
        '</div>');
    } else if (this.currentTab === 'weapons') {
      items = (this.data.weapons || []).map((w) =>
        '<div class="db-card">' +
        '<img class="db-icon" src="/res/Project/武器/' + encodeURIComponent(w.name) + '.png" alt="" onerror="this.style.visibility=\'hidden\'" />' +
        '<span class="db-name">' + escapeHtml(w.name) + '</span>' +
        '</div>');
    } else if (this.currentTab === 'resonance') {
      items = (this.data.resonance || []).map((r) => '<span class="db-item">' + escapeHtml(r.name) + '</span>');
    } else {
      items = (this.data.resonance_echoes || []).map((m) =>
        '<span class="db-item">' + escapeHtml(m.set) + '<span class="cost">C' + m.cost + '</span><span class="lvl">' + escapeHtml(m.echo) + '</span></span>');
    }
    list.innerHTML = items.join('') || '<div class="card-hint">暂无数据</div>';
  },
};
