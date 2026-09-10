/* gacha.js —— 抽卡记录：扫描游戏日志 / 链接同步 + 记录浏览（数据由 C 后端本机代理官方接口） */
const Gacha = {
  data: null,          /* { ok, uid, sync_time, total, pools: { "1":[...], ... } } */
  all: [],             /* 展平并附加序号的记录 */
  filterPool: 'all',
  filterQuality: 'all',
  page: 1,
  pageSize: 200,
  busy: false,

  /* 卡池 ID → 显示名（与上游 wuwa-gacha-tool 一致） */
  POOL_NAMES: {
    '1': '角色活动唤取', '2': '武器活动唤取', '3': '角色常驻唤取', '4': '武器常驻唤取',
    '5': '新手唤取', '6': '新手自选唤取', '7': '新手自选唤取', '8': '角色新旅唤取',
    '9': '武器新旅唤取', '10': '角色联动唤取', '11': '武器联动唤取',
    '12': '角色忆旅唤取', '13': '武器忆旅唤取',
  },

  init() {
    const $ = (id) => document.getElementById(id);
    if (!$('gachaScanBtn')) return;

    /* 回填上次使用的游戏目录 */
    Api.json('/api/gacha/config').then((cfg) => {
      if (cfg && cfg.ok && cfg.game_dir) $('gachaDirInput').value = cfg.game_dir;
    }).catch(() => {});

    $('gachaScanBtn').addEventListener('click', () => this.scanAndSync());
    $('gachaUrlToggle').addEventListener('click', () => {
      const box = $('gachaUrlBox');
      box.style.display = box.style.display === 'none' ? 'block' : 'none';
    });
    $('gachaUrlSyncBtn').addEventListener('click', async () => {
      const url = $('gachaUrlInput').value.trim();
      if (url) await this.sync(url);
    });
    $('gachaReloadBtn').addEventListener('click', () => this.load());

    /* 自绘星级下拉 */
    const dd = $('gachaQualityDD'), head = $('gachaQualityHead'), menu = $('gachaQualityMenu');
    head.addEventListener('click', (e) => {
      e.stopPropagation();
      dd.classList.toggle('open');
    });
    menu.querySelectorAll('.gacha-dd-opt').forEach((opt) => {
      opt.addEventListener('click', () => {
        this.filterQuality = opt.dataset.q;
        this.page = 1;
        $('gachaQualityLabel').textContent = opt.textContent.trim();
        menu.querySelectorAll('.gacha-dd-opt').forEach((o) => o.classList.toggle('active', o === opt));
        dd.classList.remove('open');
        this.renderList();
      });
    });
    document.addEventListener('click', () => dd.classList.remove('open'));

    this.load();
  },

  setStatus(text, color) {
    const el = document.getElementById('gachaStatus');
    el.textContent = text || '';
    el.style.color = color || 'var(--muted)';
  },

  /* 扫描 Client.log 提取链接 → 同步 */
  async scanAndSync() {
    if (this.busy) return;
    this.busy = true;
    const dir = document.getElementById('gachaDirInput').value.trim();
    this.setStatus('正在读取游戏日志并提取唤取记录链接…', 'var(--gold)');
    let url = null;
    try {
      const scan = await Api.json('/api/gacha/scan', { game_dir: dir });
      if (!scan.ok) { this.setStatus('扫描失败：' + (scan.error || '未知错误'), 'var(--red)'); return; }
      url = scan.url;
    } catch (e) {
      this.setStatus('扫描请求失败：' + e, 'var(--red)');
      return;
    } finally {
      this.busy = false;
    }
    await this.sync(url);
  },

  async sync(url) {
    if (this.busy) return;
    this.busy = true;
    this.setStatus('链接已提取，正在同步 13 类卡池的官方唤取记录（首次同步记录较多时请耐心等待）…', 'var(--gold)');
    try {
      const r = await Api.json('/api/gacha/sync', { url });
      if (r.ok) {
        this.setStatus(`同步完成：UID ${r.uid}，共 ${r.total} 条记录 · ${r.sync_time}` +
          (r.failed_pools ? `（${r.failed_pools} 个卡池拉取失败：${r.error}）` : ''), 'var(--green)');
        await this.load();
      } else {
        this.setStatus('同步失败：' + (r.error || '未知错误'), 'var(--red)');
      }
    } catch (e) {
      this.setStatus('同步请求失败：' + e, 'var(--red)');
    } finally {
      this.busy = false;
    }
  },

  async load() {
    try {
      const d = await Api.json('/api/gacha/records');
      if (!d || !d.ok) {
        this.data = null;
        document.getElementById('gachaStats').style.display = 'none';
        document.getElementById('gachaListCard').style.display = 'none';
        if (!document.getElementById('gachaStatus').textContent) {
          this.setStatus('尚无记录：请先在游戏内打开「唤取记录」页面，然后点击「扫描日志并同步」', 'var(--muted)');
        }
        return;
      }
      this.data = d;
      this.flatten(d);
      this.renderStats();
      this.renderChips();
      this.renderList();
      if (!this.busy) {
        this.setStatus(`当前 UID ${d.uid} · 共 ${d.total} 条 · 最近同步 ${d.sync_time}`, 'var(--muted)');
      }
    } catch (e) { /* 静默 */ }
  },

  /* 展平 + 计算卡池内序号与距上次五星 */
  flatten(d) {
    const rows = [];
    for (const [pid, list] of Object.entries(d.pools || {})) {
      const arr = (list || []).map((r, i) => ({ ...r, _i: i }));
      arr.sort((a, b) => a.time === b.time ? a._i - b._i : (a.time < b.time ? -1 : 1));
      let ord = 0, since5 = 0;
      for (const r of arr) {
        ord++; since5++;
        rows.push({
          pool: pid,
          poolName: this.POOL_NAMES[pid] || ('卡池' + pid),
          name: r.name || '未知',
          quality: r.qualityLevel || 3,
          type: r.resourceType || '',
          time: r.time || '',
          ord,
          gap: since5,
        });
        if (r.qualityLevel >= 5) since5 = 0;
      }
    }
    rows.sort((a, b) => a.time === b.time ? 0 : (a.time < b.time ? 1 : -1));
    this.all = rows;
  },

  renderStats() {
    const el = document.getElementById('gachaStats');
    const rows = this.all;
    const total = rows.length;
    const five = rows.filter((r) => r.quality === 5).length;
    const four = rows.filter((r) => r.quality === 4).length;
    const rate = total ? ((five / total) * 100).toFixed(2) : '0.00';
    const avg = five ? Math.round(total / five * 10) / 10 : '-';
    /* 角色活动池当前垫抽 */
    let curPity = 0;
    const charPool = this.data.pools && this.data.pools['1'];
    if (charPool) {
      const arr = charPool.slice().sort((a, b) => (a.time < b.time ? -1 : a.time > b.time ? 1 : 0));
      for (let i = arr.length - 1; i >= 0; i--) { if (arr[i].qualityLevel >= 5) break; curPity++; }
    }
    el.innerHTML = [
      [total, '累计唤取'], [five, '五星数量'],
      [rate + '%', '五星概率'], [avg, '平均五星抽数'], [curPity, '角色池当前垫抽'],
    ].map(([n, l]) => `<div class="count-card"><div class="count-num">${n}</div><div class="count-label">${l}</div></div>`).join('');
    el.style.display = 'flex';
  },

  renderChips() {
    const el = document.getElementById('gachaPoolChips');
    const counts = {};
    for (const r of this.all) counts[r.pool] = (counts[r.pool] || 0) + 1;
    let html = `<button class="gacha-chip ${this.filterPool === 'all' ? 'active' : ''}" data-pool="all">全部 ${this.all.length}</button>`;
    for (const pid of Object.keys(this.POOL_NAMES)) {
      if (!counts[pid]) continue;
      html += `<button class="gacha-chip ${this.filterPool === pid ? 'active' : ''}" data-pool="${pid}">${this.POOL_NAMES[pid]} ${counts[pid]}</button>`;
    }
    el.innerHTML = html;
    el.querySelectorAll('.gacha-chip').forEach((b) => b.addEventListener('click', () => {
      this.filterPool = b.dataset.pool; this.page = 1;
      this.renderChips(); this.renderList();
    }));
  },

  filtered() {
    return this.all.filter((r) =>
      (this.filterPool === 'all' || r.pool === this.filterPool) &&
      (this.filterQuality === 'all' || String(r.quality) === this.filterQuality)
    );
  },

  iconUrl(r) {
    if (r.type === '角色') return '/res/Project/角色头像/' + encodeURIComponent(r.name) + '.png';
    /* 常驻池/武器池里武器会以“道具”类型发放，图标同样走武器目录 */
    if (r.type === '武器' || r.type === '道具') return '/res/Project/武器/' + encodeURIComponent(r.name) + '.png';
    return '';
  },

  renderList() {
    const card = document.getElementById('gachaListCard');
    const listEl = document.getElementById('gachaList');
    const pagerEl = document.getElementById('gachaPager');
    if (!this.all.length) { card.style.display = 'none'; return; }
    card.style.display = 'flex';
    card.style.flexDirection = 'column';

    const rows = this.filtered();
    const pages = Math.max(1, Math.ceil(rows.length / this.pageSize));
    if (this.page > pages) this.page = pages;
    const slice = rows.slice((this.page - 1) * this.pageSize, this.page * this.pageSize);

    listEl.innerHTML = slice.map((r) => {
      const stars = '★'.repeat(r.quality);
      const qcls = 'q' + r.quality;
      const icon = this.iconUrl(r);
      const img = icon
        ? `<img class="gacha-icon" src="${icon}" loading="lazy" onerror="this.style.visibility='hidden'" />`
        : `<div class="gacha-icon gacha-icon-empty"></div>`;
      const gap = r.quality === 5 ? `<span class="gacha-gap">第 ${r.gap} 抽</span>` : '';
      return `<div class="gacha-row ${qcls}">
        ${img}
        <div class="gacha-main">
          <span class="gacha-name">${r.name}</span>
          <span class="gacha-stars">${stars}</span>
          <span class="gacha-type">${r.type}</span>
        </div>
        <div class="gacha-side">
          <span class="gacha-pool">${r.poolName}</span>
          <span class="gacha-time">${r.time}</span>
        </div>
        ${gap}
      </div>`;
    }).join('');

    if (pages > 1) {
      pagerEl.style.display = 'flex';
      pagerEl.innerHTML = `
        <button class="btn ghost" ${this.page <= 1 ? 'disabled' : ''} id="gachaPrev">上一页</button>
        <span class="gacha-pageinfo">${this.page} / ${pages} 页 · 共 ${rows.length} 条</span>
        <button class="btn ghost" ${this.page >= pages ? 'disabled' : ''} id="gachaNext">下一页</button>`;
      const prev = document.getElementById('gachaPrev'), next = document.getElementById('gachaNext');
      if (prev) prev.addEventListener('click', () => { this.page--; this.renderList(); listEl.scrollTop = 0; });
      if (next) next.addEventListener('click', () => { this.page++; this.renderList(); listEl.scrollTop = 0; });
    } else {
      pagerEl.style.display = 'none';
      pagerEl.innerHTML = '';
    }
  },
};

window.initGachaView = function () {
  if (!Gacha.data) Gacha.load();
};
