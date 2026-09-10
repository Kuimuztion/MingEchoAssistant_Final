/* 全息战略 — 挑战数据展示 */
const Holo = {
  data: null,

  async init() {
    await this.loadData();
  },

  async loadData() {
    const status = document.getElementById('holoStatus');
    const content = document.getElementById('holoContent');
    status.style.display = 'block';
    content.style.display = 'none';
    status.textContent = '正在加载挑战数据…';
    status.style.color = 'var(--text3)';

    try {
      /* 从 API 配置读取 roleId/serverId（不依赖输入框时序） */
      const cfg = await Api.json('/api/kuro/config');
      const roleId = (cfg.role_id || '').trim();
      const serverId = (cfg.server_id || '').trim();
      if (!roleId || !serverId) {
        status.textContent = '请先在「游戏账户」中连接库街区账号。';
        status.style.color = 'var(--gold)';
        return;
      }

      /* 获取 b-at */
      const bat = await Game.getBat();
      if (!bat) {
        status.textContent = '获取访问凭证失败，请检查游戏账户连接状态。';
        status.style.color = 'var(--red)';
        return;
      }

      const form = 'gameId=3&serverId=' + encodeURIComponent(serverId) + '&roleId=' + encodeURIComponent(roleId) + '&countryCode=1';
      const body = { path: '/aki/roleBox/akiBox/challengeDetails', form: form, headers: JSON.stringify({ 'b-at': bat }) };
      const resp = await Api.json('/api/kuro/proxy', body);

      if (resp.code !== 200 && resp.code !== 10902) {
        /* token 过期重试 */
        if (resp.code === 10900 || resp.code === 220 || resp.code === 10901) {
          Game.bat = null;
          Game.batTime = 0;
          const bat2 = await Game.getBat();
          if (bat2) {
            body.headers = JSON.stringify({ 'b-at': bat2 });
            const resp2 = await Api.json('/api/kuro/proxy', body);
            if (resp2.code === 200 || resp2.code === 10902) {
              this.data = JSON.parse(resp2.data);
            } else {
              status.textContent = '加载失败（code=' + resp2.code + '）：' + (resp2.msg || '');
              status.style.color = 'var(--red)';
              return;
            }
          }
        } else {
          status.textContent = '加载失败（code=' + resp.code + '）：' + (resp.msg || '');
          status.style.color = 'var(--red)';
          return;
        }
      } else {
        this.data = JSON.parse(resp.data);
      }

      if (!this.data || !this.data.open) {
        status.textContent = '挑战数据未公开。请在库街区 App → 数据终端 → 设置中开启「对外展示挑战数据」。';
        status.style.color = 'var(--gold)';
        return;
      }

      status.style.display = 'none';
      content.style.display = 'block';
      this.render();
    } catch (e) {
      console.error('Holo loadData error:', e);
      status.textContent = '加载异常：' + String(e);
      status.style.color = 'var(--red)';
    }
  },

  render() {
    const content = document.getElementById('holoContent');
    if (!this.data || !this.data.challengeInfo) {
      content.innerHTML = '<div style="padding:20px;color:var(--text3)">暂无挑战数据</div>';
      return;
    }

    const ci = this.data.challengeInfo;

    /* 按 Boss 分组（不区分模式，API 不返回模式信息） */
    const bosses = {};
    for (const [catId, challenges] of Object.entries(ci)) {
      for (const c of challenges) {
        const name = c.bossName || '未知Boss';
        if (!bosses[name]) {
          bosses[name] = {
            name: name,
            icon: c.bossHeadIcon || c.bossIconUrl || '',
            difficulties: {}
          };
        }
        bosses[name].difficulties[c.difficulty] = c;
      }
    }

    /* 统计 */
    let totalCleared = 0;
    let totalPossible = 0;
    const bossList = Object.values(bosses);
    for (const b of bossList) {
      for (let d = 1; d <= 6; d++) {
        totalPossible++;
        if (b.difficulties[d]) totalCleared++;
      }
    }

    let html = '';
    /* 汇总 */
    html += '<div class="holo-summary">';
    html += '<div class="holo-summary-item">Boss数量：<b>' + bossList.length + '</b></div>';
    html += '<div class="holo-summary-item">已通关：<b>' + totalCleared + '/' + totalPossible + '</b></div>';
    html += '<div class="holo-summary-item">完成度：<b>' + (totalPossible > 0 ? Math.round(totalCleared / totalPossible * 100) : 0) + '%</b></div>';
    html += '</div>';

    /* Boss 竖排列表（和声骸排版一致） */
    html += '<div class="holo-boss-list">';
    for (const b of bossList) {
      const cleared = Object.keys(b.difficulties).length;
      html += '<div class="holo-boss-card">';
      /* Boss 图标 */
      html += '<div class="holo-boss-icon">' + (b.icon ? '<img src="' + b.icon + '" onerror="this.style.display=\'none\'" />' : '') + '</div>';
      /* Boss 信息 */
      html += '<div class="holo-boss-info">';
      html += '<div class="holo-boss-name">' + this.escapeHtml(b.name) + '</div>';
      html += '<div class="holo-boss-meta">已通关 ' + cleared + '/6 难度</div>';
      /* 难度1-6 */
      html += '<div class="holo-diff-row">';
      for (let d = 1; d <= 6; d++) {
        const rec = b.difficulties[d];
        if (rec) {
          const teamNames = (rec.roles || []).map(r => r.roleName).join('、');
          html += '<div class="holo-diff-cell cleared">';
          html += '<div class="diff-label">全息' + d + '</div>';
          html += '<div class="diff-time">已通过</div>';
          if (teamNames) html += '<div class="diff-team">出战：' + this.escapeHtml(teamNames) + '</div>';
          html += '</div>';
        } else {
          html += '<div class="holo-diff-cell"><div class="diff-label">全息' + d + '</div><div class="diff-time">暂无记录</div></div>';
        }
      }
      html += '</div>';
      html += '</div>';
      html += '</div>';
    }
    html += '</div>';

    content.innerHTML = html;
  },

  escapeHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  },

  /* 解析通关用时：官方正常为秒数，但部分记录可能返回浮点、数字字符串
     或挂在其它字段名下；全部尝试失败时回退为"已通关"，不再显示"—" */
  formatPassTime(rec) {
    const candidates = [rec.passTime, rec.pass_time, rec.costTime, rec.useTime, rec.passTimeSecond, rec.time];
    for (const v of candidates) {
      if (v === undefined || v === null || v === '') continue;
      const n = Number(v);
      if (Number.isFinite(n) && n > 0) {
        const total = Math.round(n);
        const m = Math.floor(total / 60), s = total % 60;
        return m > 0 ? m + '分' + (s < 10 ? '0' : '') + s + '秒' : s + '秒';
      }
      if (typeof v === 'string') {
        /* 兼容 "1:32" / "1分32秒" / "92s" 等字符串格式 */
        const m2 = v.match(/^\s*(?:(\d+)\s*(?::|分|′))?\s*(\d+(?:\.\d+)?)\s*(?:秒|s|"|″)?\s*$/);
        if (m2) {
          const mm = parseInt(m2[1] || '0', 10);
          const ss = Math.round(parseFloat(m2[2]));
          const total = mm * 60 + ss;
          if (total > 0) return mm > 0 ? mm + '分' + (ss < 10 ? '0' : '') + ss + '秒' : ss + '秒';
        }
      }
    }
    return '已通关';
  },
};
