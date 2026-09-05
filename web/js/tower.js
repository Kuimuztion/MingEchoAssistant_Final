/* 逆境深塔 — 深塔数据展示 */
const Tower = {
  data: null,
  roleNameMap: {},

  async init() {
    /* 构建 roleId -> roleName 映射 */
    if (Game.characters && Game.characters.length > 0) {
      for (const c of Game.characters) {
        if (c.roleId && c.roleName) {
          this.roleNameMap[c.roleId] = c.roleName;
        }
      }
    }
    await this.loadData();
  },

  async loadData() {
    const status = document.getElementById('towerStatus');
    const content = document.getElementById('towerContent');
    status.style.display = 'block';
    content.style.display = 'none';
    status.textContent = '正在加载深塔数据…';
    status.style.color = 'var(--text3)';

    try {
      const cfg = await Api.json('/api/kuro/config');
      const roleId = (cfg.role_id || '').trim();
      const serverId = (cfg.server_id || '').trim();
      if (!roleId || !serverId) {
        status.textContent = '请先在「游戏账户」中连接库街区账号。';
        status.style.color = 'var(--gold)';
        return;
      }

      const bat = await Game.getBat();
      if (!bat) {
        status.textContent = '获取访问凭证失败，请检查游戏账户连接状态。';
        status.style.color = 'var(--red)';
        return;
      }

      const form = 'gameId=3&serverId=' + encodeURIComponent(serverId) + '&roleId=' + encodeURIComponent(roleId);
      const body = { path: '/aki/roleBox/akiBox/towerDataDetail', form: form, headers: JSON.stringify({ 'b-at': bat }) };
      let resp = await Api.json('/api/kuro/proxy', body);

      /* token 过期重试 */
      if (resp.code === 10900 || resp.code === 220 || resp.code === 10901) {
        Game.bat = null;
        Game.batTime = 0;
        const bat2 = await Game.getBat();
        if (bat2) {
          body.headers = JSON.stringify({ 'b-at': bat2 });
          resp = await Api.json('/api/kuro/proxy', body);
        }
      }

      if (resp.code !== 200 && resp.code !== 10902) {
        status.textContent = '加载失败（code=' + resp.code + '）：' + (resp.msg || '');
        status.style.color = 'var(--red)';
        return;
      }

      this.data = JSON.parse(resp.data);
      if (!this.data || !this.data.difficultyList || this.data.difficultyList.length === 0) {
        status.textContent = '暂无深塔数据。请在库街区 App → 数据终端 → 设置中开启「对外展示深塔数据」。';
        status.style.color = 'var(--gold)';
        return;
      }

      status.style.display = 'none';
      content.style.display = 'block';
      this.render();
    } catch (e) {
      console.error('Tower loadData error:', e);
      status.textContent = '加载异常：' + String(e);
      status.style.color = 'var(--red)';
    }
  },

  render() {
    const content = document.getElementById('towerContent');
    const dl = this.data.difficultyList;

    /* 统计总星数 */
    let totalStars = 0;
    let totalMaxStars = 0;
    for (const diff of dl) {
      for (const area of (diff.towerAreaList || [])) {
        totalStars += area.star || 0;
        totalMaxStars += area.maxStar || 0;
      }
    }

    let html = '';
    /* 汇总 */
    html += '<div class="tower-summary">';
    html += '<div class="tower-summary-item">总星数：<b>' + totalStars + '/' + totalMaxStars + '</b></div>';
    html += '<div class="tower-summary-item">难度区：<b>' + dl.length + '</b></div>';
    html += '</div>';

    /* 按难度区渲染 */
    for (const diff of dl) {
      html += '<div class="tower-difficulty">';
      html += '<div class="tower-diff-title">' + this.escapeHtml(diff.difficultyName || ('难度' + diff.difficulty)) + '</div>';

      for (const area of (diff.towerAreaList || [])) {
        html += '<div class="tower-area">';
        /* 塔标题 + 星数 */
        html += '<div class="tower-area-header">';
        html += '<div class="tower-area-name">' + this.escapeHtml(area.areaName || '未知塔') + '</div>';
        html += '<div class="tower-area-stars">' + (area.star || 0) + ' / ' + (area.maxStar || 0) + ' ★</div>';
        html += '</div>';

        /* 楼层列表（只渲染 floorList 中实际存在的楼层） */
        html += '<div class="tower-floor-list">';
        const floors = area.floorList || [];
        for (const floor of floors) {
          const f = floor.floor;
          if (floor.star > 0) {
            html += '<div class="tower-floor cleared">';
            html += '<div class="tower-floor-num">第 ' + f + ' 层</div>';
            /* 星级 */
            let starsHtml = '';
            for (let s = 1; s <= 3; s++) {
              starsHtml += s <= (floor.star || 0) ? '★' : '<span class="empty">★</span>';
            }
            html += '<div class="tower-floor-stars">' + starsHtml + '</div>';
            /* 出战角色头像 */
            if (floor.roleList && floor.roleList.length > 0) {
              html += '<div class="tower-floor-roles">';
              for (const r of floor.roleList) {
                const roleName = this.roleNameMap[r.roleId] || '';
                html += '<div class="tower-floor-role" title="' + this.escapeHtml(roleName) + '">';
                if (r.iconUrl) html += '<img src="' + r.iconUrl + '" onerror="this.style.display=\'none\'" />';
                html += '</div>';
              }
              html += '</div>';
            }
            html += '</div>';
          } else {
            html += '<div class="tower-floor">';
            html += '<div class="tower-floor-num">第 ' + f + ' 层</div>';
            html += '<div class="tower-floor-stars"><span class="empty">★★★</span></div>';
            html += '<div style="font-size:11px;color:var(--text3);margin-top:4px">未通关</div>';
            html += '</div>';
          }
        }
        html += '</div>';
        html += '</div>';
      }
      html += '</div>';
    }

    content.innerHTML = html;
  },

  escapeHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  },
};
