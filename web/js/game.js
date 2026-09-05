/* game.js —— 游戏账户：库街区 API 连接、角色列表、角色详情（面板+声骸） */
const DEFAULT_SERVER_ID = '76402e5b20be2c39f095a152090afddc'; // 国服默认区服ID
const Game = {
  configured: false,
  characters: [],
  currentChar: null,
  bat: null, // b-at access token
  batTime: 0, // b-at token 获取时间戳（5分钟自动刷新）

  async init() {
    document.getElementById('kuroConnectBtn').addEventListener('click', () => this.connect());
    document.getElementById('kuroHelpBtn').addEventListener('click', () => this.toggleHelp());
    const smsHelpBtn = document.getElementById('kuroSmsHelpBtn');
    if (smsHelpBtn) smsHelpBtn.addEventListener('click', () => this.toggleSmsHelp());
    const smsBtn = document.getElementById('kuroSmsLoginBtn');
    if (smsBtn) smsBtn.addEventListener('click', () => this.openSmsLogin());
    /* 加载已保存的配置 */
    const cfg = await Api.json('/api/kuro/config');
    if (cfg.ok && cfg.configured) {
      this.configured = true;
      document.getElementById('kuroToken').placeholder = '已保存（' + cfg.token + '），如需更换请直接输入新 Token';
      document.getElementById('kuroRoleId').value = cfg.role_id || '';
      document.getElementById('kuroServerId').value = cfg.server_id || DEFAULT_SERVER_ID;
      document.getElementById('kuroConnectBtn').textContent = '重新连接';
      /* 自动加载角色列表 */
      this.loadCharacters();
    }
  },

  openSmsLogin() {
    window.open('/kuro-login.html', 'kuro_sms_login', 'width=480,height=680');
  },

  toggleHelp() {
    const t = document.getElementById('kuroTutorial');
    const sms = document.getElementById('kuroSmsTutorial');
    if (sms) sms.style.display = 'none';
    t.style.display = t.style.display === 'none' ? 'block' : 'none';
  },

  toggleSmsHelp() {
    const t = document.getElementById('kuroSmsTutorial');
    const token = document.getElementById('kuroTutorial');
    if (token) token.style.display = 'none';
    t.style.display = t.style.display === 'none' ? 'block' : 'none';
  },

  async connect() {
    const tokenInput = document.getElementById('kuroToken');
    const token = tokenInput.value.trim();
    const roleId = document.getElementById('kuroRoleId').value.trim();
    const serverId = document.getElementById('kuroServerId').value.trim();
    const msg = document.getElementById('kuroConnectMsg');
    const btn = document.getElementById('kuroConnectBtn');

    /* token 为空时使用本地已保存的配置连接（短信登录后 token 已存本地） */
    const useSaved = !token;
    if (!roleId) { msg.textContent = '请输入游戏角色 ID（roleId）'; msg.style.color = 'var(--red)'; return; }

    btn.disabled = true;
    btn.textContent = '连接中…';
    msg.textContent = useSaved ? '正在使用已保存的凭证连接…' : '正在保存并验证库街区连接…';
    msg.style.color = '';

    try {
      if (!useSaved) {
        /* 1. 保存配置（仅当用户手动输入了新 token 时） */
        const save = await Api.json('/api/kuro/config', { token, role_id: roleId, server_id: serverId });
        if (!save.ok) { msg.textContent = '保存失败：' + (save.error || ''); msg.style.color = 'var(--red)'; return; }
        this.configured = true;
      }
      this.bat = null;

      /* 2. 验证 token：获取 b-at 并加载角色列表 */
      msg.textContent = '正在从库街区获取角色列表…';
      await this.loadCharacters();
      btn.textContent = '重新连接';
    } catch (e) {
      msg.textContent = '连接异常：' + String(e);
      msg.style.color = 'var(--red)';
    } finally {
      btn.disabled = false;
    }
  },

  /* 获取 b-at access token（调用 /aki/roleBox/requestToken），5分钟自动刷新 */
  async getBat() {
    const NOW = Date.now();
    if (this.bat && this.batTime && (NOW - this.batTime) < 5 * 60 * 1000) return this.bat;
    this.bat = null;
    const roleId = document.getElementById('kuroRoleId').value.trim();
    const serverId = document.getElementById('kuroServerId').value.trim();
    const data = await Api.json('/api/kuro/proxy', {
      path: '/aki/roleBox/requestToken',
      form: 'serverId=' + encodeURIComponent(serverId) + '&roleId=' + encodeURIComponent(roleId)
    });
    if (data.code === 200 && data.data) {
      try {
        const parsed = JSON.parse(data.data);
        this.bat = parsed.accessToken || null;
        this.batTime = NOW;
        return this.bat;
      } catch (e) {
        return null;
      }
    }
    return null;
  },

  /* 刷新游戏数据（调用 /aki/roleBox/akiBox/refreshData） */
  async refreshData() {
    const roleId = document.getElementById('kuroRoleId').value.trim();
    const serverId = document.getElementById('kuroServerId').value.trim();
    const bat = await this.getBat();
    if (!bat) return false;
    const data = await Api.json('/api/kuro/proxy', {
      path: '/aki/roleBox/akiBox/refreshData',
      form: 'gameId=3&serverId=' + encodeURIComponent(serverId) + '&roleId=' + encodeURIComponent(roleId),
      headers: JSON.stringify({ 'b-at': bat })
    });
    return data.code === 200 || data.code === 10902;
  },

  /* 通用代理调用（支持 b-at） */
  async proxy(path, form, useBat = true) {
    const body = { path, form };
    if (useBat) {
      const bat = await this.getBat();
      if (bat) body.headers = JSON.stringify({ 'b-at': bat });
    }
    return await Api.json('/api/kuro/proxy', body);
  },

  /* 解析 API 返回的 data 字段（可能是 JSON 字符串） */
  parseData(resp) {
    if (!resp) return null;
    if (typeof resp.data === 'string') {
      try { return JSON.parse(resp.data); } catch (e) { return resp.data; }
    }
    return resp.data;
  },

  async loadCharacters() {
    const msg = document.getElementById('kuroConnectMsg');
    const roleId = document.getElementById('kuroRoleId').value.trim();
    const serverId = document.getElementById('kuroServerId').value.trim();

    /* 1. 获取 b-at */
    msg.textContent = '正在获取访问凭证…';
    const bat = await this.getBat();
    if (!bat) {
      msg.textContent = '获取访问凭证失败（Token 可能已过期）。请点击「短信登录」重新登录，或检查 Token 是否正确。';
      msg.style.color = 'var(--red)';
      return;
    }

    /* 2. 刷新数据 */
    msg.textContent = '正在刷新游戏数据…';
    await this.refreshData();

    /* 3. 获取角色列表：/aki/roleBox/akiBox/roleData */
    msg.textContent = '正在从库街区获取角色列表…';
    const form = 'gameId=3&roleId=' + encodeURIComponent(roleId) + '&serverId=' + encodeURIComponent(serverId);
    const data = await this.proxy('/aki/roleBox/akiBox/roleData', form);

    const listCard = document.getElementById('kuroCharListCard');
    const grid = document.getElementById('kuroCharGrid');
    grid.innerHTML = '';

    if (data.code !== 200 && data.code !== 10902) {
      msg.textContent = '获取角色列表失败（code=' + data.code + '）：' + (data.msg || JSON.stringify(data).slice(0, 120));
      msg.style.color = 'var(--red)';
      listCard.style.display = 'block';
      grid.innerHTML = '<div style="padding:20px;color:var(--text3);font-size:13px">接口返回错误。请检查 Token 是否有效、roleId/serverId 是否正确。<br><br>原始响应：<pre style="white-space:pre-wrap;margin-top:8px;font-size:11px;color:var(--text2)">' + this.escapeHtml(JSON.stringify(data, null, 2).slice(0, 2000)) + '</pre></div>';
      return;
    }

    /* 解析角色列表 —— data 字段是 JSON 字符串 */
    const parsed = this.parseData(data);
    let chars = [];
    if (parsed && parsed.roleList && Array.isArray(parsed.roleList)) chars = parsed.roleList;
    else if (parsed && Array.isArray(parsed)) chars = parsed;
    else if (parsed && parsed.list && Array.isArray(parsed.list)) chars = parsed.list;

    this.characters = chars;
    document.getElementById('kuroCharCount').textContent = '（共 ' + chars.length + ' 个）';

    if (chars.length === 0) {
      msg.textContent = '连接成功，但未获取到角色数据。可能需要在库街区 App 的「数据终端」中开启「对外展示」。';
      msg.style.color = 'var(--gold)';
      listCard.style.display = 'block';
      grid.innerHTML = '<div style="padding:20px;color:var(--text3);font-size:13px">未解析到角色列表。请在库街区 App → 数据终端 → 设置中开启「对外展示角色」。<br><br>原始响应：<pre style="white-space:pre-wrap;margin-top:8px;font-size:11px;color:var(--text2)">' + this.escapeHtml(JSON.stringify(parsed || data, null, 2).slice(0, 3000)) + '</pre></div>';
      return;
    }

    msg.textContent = '连接成功！获取到 ' + chars.length + ' 个角色。点击角色查看面板与声骸详情。';
    msg.style.color = 'var(--green)';
    listCard.style.display = 'block';

    /* 渲染角色卡片 */
    chars.forEach((c, i) => {
      const name = c.roleName || c.name || c.characterName || c.role_name || ('角色' + (i + 1));
      const level = c.level || c.roleLevel || c.lv || '-';
      const card = document.createElement('div');
      card.className = 'kuro-char-card';
      /* 本地头像优先，失败则回退到 API 图标，再失败显示默认 */
      const localAvatar = '/res/Project/角色头像/' + encodeURIComponent(name) + '.png';
      const apiIcon = c.roleIconUrl || c.iconUrl || c.headIcon || c.avatar || c.rolePicUrl || '';
      card.innerHTML =
        '<div class="kc-icon"><img src="' + localAvatar + '" onerror="this.onerror=null;this.src=\'' + (apiIcon || '') + '\';this.onerror=function(){this.style.display=\'none\';this.parentNode.textContent=\'◆\'};" /></div>' +
        '<div class="kc-name">' + this.escapeHtml(String(name)) + '</div>' +
        '<div class="kc-level">Lv.' + level + '</div>';
      card.onclick = () => this.showCharacterDetail(c, card);
      grid.appendChild(card);
    });
  },

  async showCharacterDetail(char, cardEl) {
    /* 高亮选中 */
    document.querySelectorAll('.kuro-char-card').forEach(c => c.classList.remove('active'));
    cardEl.classList.add('active');

    const detailCard = document.getElementById('kuroCharDetailCard');
    const detail = document.getElementById('kuroCharDetail');
    const title = document.getElementById('kuroDetailTitle');
    const name = char.roleName || char.name || char.characterName || '角色';
    title.textContent = name + ' — 角色详情';
    detailCard.style.display = 'block';
    detail.innerHTML = '<div style="padding:20px;color:var(--text3)">正在加载角色详情…</div>';

    /* 评分按钮 */
    const scoreBtn = document.getElementById('kuroScoreBtn');
    const charId = char.roleId || char.id || char.characterId || char.role_id || '';
    const roleId = document.getElementById('kuroRoleId').value.trim();
    const serverId = document.getElementById('kuroServerId').value.trim();
    const charIcon = char.roleIconUrl || char.iconUrl || char.headIcon || '';
    if (scoreBtn && charId && roleId && serverId) {
      scoreBtn.style.display = '';
      scoreBtn.onclick = () => {
        const url = '/char_score.html?roleId=' + encodeURIComponent(roleId)
          + '&serverId=' + encodeURIComponent(serverId)
          + '&charId=' + encodeURIComponent(charId)
          + '&name=' + encodeURIComponent(name)
          + '&icon=' + encodeURIComponent(charIcon);
        window.open(url, '_blank');
      };
    } else if (scoreBtn) {
      scoreBtn.style.display = 'none';
    }

    if (!charId) {
      detail.innerHTML = this.renderDetailFromRaw(char, null);
      return;
    }

    try {
      const form = 'serverId=' + encodeURIComponent(serverId) + '&roleId=' + encodeURIComponent(roleId) + '&id=' + encodeURIComponent(charId);

      let data = await this.proxy('/aki/roleBox/akiBox/getRoleDetail', form);

      /* token 过期（10900/220/10901）时，强制刷新 b-at 后重试一次 */
      if (data.code === 10900 || data.code === 220 || data.code === 10901) {
        this.bat = null;
        this.batTime = 0;
        detail.innerHTML = '<div style="padding:20px;color:var(--gold)">凭证已过期，正在重新获取…</div>';
        data = await this.proxy('/aki/roleBox/akiBox/getRoleDetail', form);
      }

      if (data.code !== 200 && data.code !== 10902) {
        detail.innerHTML = '<div style="padding:16px;color:var(--red);font-size:13px">加载失败（code=' + data.code + '）：' + this.escapeHtml(data.msg || JSON.stringify(data).slice(0, 200)) + '<br><br>请尝试重新连接游戏账户。</div>';
        return;
      }

      const parsed = this.parseData(data);
      detail.innerHTML = this.renderDetail(parsed || char, char);
    } catch (e) {
      console.error('showCharacterDetail error:', e);
      detail.innerHTML = '<div style="padding:16px;color:var(--red);font-size:13px">加载异常：' + this.escapeHtml(String(e)) + '<br><br>请刷新页面后重试，或检查网络连接。</div>';
    }
  },

  /* 从角色数据渲染详情（面板属性 + 声骸） */
  renderDetail(data, char) {
    const role = data.role || data;
    const esc = s => this.escapeHtml(String(s ?? '-'));
    let html = '';

    /* 角色基础信息 */
    html += '<div class="kuro-section-title">角色基础信息</div>';
    html += '<div class="kuro-panel-grid">';
    const star = role.starLevel ? '★'.repeat(role.starLevel) : '';
    const fields = [
      ['角色名', (role.roleName || char?.roleName || '-') + (star ? ' <span style="color:var(--gold);font-size:11px">' + star + '</span>' : '')],
      ['等级', role.level || char?.level || '-'],
      ['共鸣链', role.chainUnlockNum !== undefined ? role.chainUnlockNum : (char?.chainUnlockNum ?? '-')],
      ['元素', role.attributeName || role.element || '-'],
      ['突破', role.breach !== undefined ? role.breach : '-'],
    ];
    fields.forEach(([k, v]) => {
      html += '<div class="kuro-panel-item"><div class="kpi-label">' + k + '</div><div class="kpi-value">' + v + '</div></div>';
    });
    html += '</div>';

    /* 武器信息（顶层 weaponData，含 weapon 子对象） */
    const wd = data.weaponData || {};
    const winfo = wd.weapon || {};
    if (wd && Object.keys(wd).length > 0) {
      html += '<div class="kuro-section-title">武器</div>';
      const star = winfo.weaponStarLevel ? '★'.repeat(winfo.weaponStarLevel) : '';
      const starColor = winfo.weaponStarLevel >= 5 ? 'var(--gold)' : (winfo.weaponStarLevel >= 4 ? 'var(--purple,#a78bfa)' : 'var(--blue,#6fc3df)');
      html += '<div style="background:var(--bg-3);border:1px solid var(--border);border-radius:8px;padding:14px;margin:8px 0;display:flex;gap:14px;align-items:flex-start">';
      /* 武器立绘图标 */
      if (winfo.weaponIcon) {
        html += '<img src="' + winfo.weaponIcon + '" style="width:64px;height:64px;border-radius:10px;flex-shrink:0;background:var(--bg-2)" onerror="this.style.display=\'none\'" />';
      } else {
        html += '<div style="width:64px;height:64px;border-radius:10px;flex-shrink:0;background:var(--bg-2);display:flex;align-items:center;justify-content:center;color:var(--gold);font-size:28px">⚔</div>';
      }
      html += '<div style="flex:1;min-width:0">';
      /* 武器名 + 星级 */
      html += '<div style="font-size:15px;font-weight:700;color:var(--text);margin-bottom:4px">' + esc(winfo.weaponName || '未知武器') +
        ' <span style="color:' + starColor + ';font-size:13px">' + star + '</span></div>';
      /* 等级/突破/共鸣等级 */
      html += '<div style="font-size:12px;color:var(--text3);margin-bottom:8px;display:flex;gap:14px;flex-wrap:wrap">' +
        '<span>等级 <b style="color:var(--text2)">' + esc(wd.level) + '</b></span>' +
        '<span>突破 <b style="color:var(--text2)">' + esc(wd.breach) + '</b></span>' +
        (wd.resonLevel !== undefined ? '<span>共鸣等级 <b style="color:var(--text2)">' + esc(wd.resonLevel) + '</b></span>' : '') +
        '</div>';
      /* 武器主属性 */
      if (Array.isArray(wd.mainPropList) && wd.mainPropList.length > 0) {
        html += '<div style="display:flex;gap:14px;flex-wrap:wrap;margin-bottom:8px">';
        wd.mainPropList.forEach(p => {
          html += '<span style="font-size:12px;color:var(--ice)">' + esc(p.attributeName) + ' <b>' + esc(p.attributeValue) + '</b></span>';
        });
        html += '</div>';
      }
      /* 武器效果 */
      if (winfo.weaponEffectName || winfo.effectDescription) {
        html += '<div style="background:rgba(0,0,0,0.2);border-radius:6px;padding:8px 10px">';
        if (winfo.weaponEffectName) html += '<div style="font-size:12px;font-weight:600;color:var(--gold);margin-bottom:3px">' + esc(winfo.weaponEffectName) + '</div>';
        if (winfo.effectDescription) html += '<div style="font-size:11.5px;color:var(--text2);line-height:1.6">' + esc(winfo.effectDescription) + '</div>';
        html += '</div>';
      }
      html += '</div></div>';
    }

    /* 面板属性（顶层 roleAttributeList） */
    const ral = data.roleAttributeList || [];
    if (Array.isArray(ral) && ral.length > 0) {
      html += '<div class="kuro-section-title">面板属性</div>';
      html += '<div class="kuro-panel-grid">';
      ral.forEach(p => {
        html += '<div class="kuro-panel-item"><div class="kpi-label">' + esc(p.attributeName) + '</div><div class="kpi-value">' + esc(p.attributeValue) + '</div></div>';
      });
      html += '</div>';
    }

    /* 声骸套装效果 —— 按套装分组，显示所有套装（x3+x2 等混合搭配） */
    const pd = data.phantomData || {};
    const echoes = pd.equipPhantomList || [];
    if (echoes.length > 0) {
      const setMap = {};
      echoes.forEach(e => {
        if (!e) return;
        const fd = e.fetterDetail || {};
        if (fd.name && fd.groupId !== undefined) {
          if (!setMap[fd.groupId]) {
            setMap[fd.groupId] = {
              name: fd.name, num: fd.num, count: 0,
              firstDescription: fd.firstDescription,
              secondDescription: fd.secondDescription,
              tripleDescription: fd.tripleDescription,
              iconUrl: fd.iconUrl
            };
          }
          setMap[fd.groupId].count++;
        }
      });
      const sets = Object.values(setMap);
      if (sets.length > 0) {
        html += '<div class="kuro-section-title">声骸套装（' + sets.length + ' 套）</div>';
        sets.forEach(s => {
          html += '<div style="background:var(--bg-3);border:1px solid var(--border);border-radius:8px;padding:12px;margin:8px 0;display:flex;gap:12px;align-items:flex-start">';
          if (s.iconUrl) html += '<img src="' + s.iconUrl + '" style="width:40px;height:40px;border-radius:6px;flex-shrink:0" onerror="this.style.display=\'none\'" />';
          html += '<div style="flex:1;min-width:0">';
          html += '<div style="color:var(--gold);font-weight:600;font-size:13px;margin-bottom:6px">' + esc(s.name) + ' <span style="color:var(--ice)">×' + esc(s.count) + '件</span> <span style="color:var(--text3);font-size:11px">（触发' + esc(s.num) + '件套效果）</span></div>';
          const descs = [s.firstDescription, s.secondDescription, s.tripleDescription].filter(d => d && d.trim());
          if (descs.length > 0) {
            descs.forEach((d, i) => {
              const color = i === 0 ? 'var(--text2)' : 'var(--text3)';
              const size = i === 0 ? '12px' : '11px';
              const mt = i === 0 ? '' : 'margin-top:4px;';
              html += '<div style="color:' + color + ';font-size:' + size + ';line-height:1.6;' + mt + '">' + esc(d) + '</div>';
            });
          } else {
            html += '<div style="color:var(--text3);font-size:11px;line-height:1.6;font-style:italic">（套装效果数据暂未开放）</div>';
          }
          html += '</div></div>';
        });
      }

      /* 装配声骸列表 —— 每个声骸前加立绘图标 */
      /* 过滤掉空槽位（null） */
      const validEchoes = echoes.filter(e => e && e.phantomProp);
      html += '<div class="kuro-section-title">装配声骸（' + validEchoes.length + ' 件，总COST ' + esc(pd.cost) + '）</div>';
      html += '<div class="kuro-echo-list">';
      validEchoes.forEach(e => {
        const pp = e.phantomProp || {};
        const ename = pp.name || '声骸';
        const qualityColor = e.quality >= 5 ? 'var(--gold)' : (e.quality >= 4 ? 'var(--purple,#a78bfa)' : 'var(--blue,#6fc3df)');
        html += '<div class="kuro-echo-item" style="border-left:3px solid ' + qualityColor + ';display:flex;gap:12px;align-items:flex-start">';
        /* 声骸立绘图标 */
        if (pp.iconUrl) {
          html += '<img src="' + pp.iconUrl + '" style="width:48px;height:48px;border-radius:8px;flex-shrink:0;background:var(--bg-2)" onerror="this.style.display=\'none\'" />';
        } else {
          html += '<div style="width:48px;height:48px;border-radius:8px;flex-shrink:0;background:var(--bg-2);display:flex;align-items:center;justify-content:center;color:var(--gold);font-size:20px">◆</div>';
        }
        html += '<div style="flex:1;min-width:0">';
        html += '<div class="ke-name">' + esc(ename) +
          ' <span style="color:' + qualityColor + ';font-size:11px">COST' + esc(e.cost) + '</span>' +
          ' <span style="color:var(--text3);font-size:11px">Lv.' + esc(e.level) + '</span>' +
          '</div>';
        /* 主词条 */
        if (Array.isArray(e.mainProps) && e.mainProps.length > 0) {
          html += '<div class="ke-main">';
          e.mainProps.forEach(m => {
            html += '<span style="margin-right:10px">' + esc(m.attributeName) + ' <b>' + esc(m.attributeValue) + '</b></span>';
          });
          html += '</div>';
        }
        /* 副词条 */
        if (Array.isArray(e.subProps) && e.subProps.length > 0) {
          html += '<div class="ke-subs">';
          e.subProps.forEach(s => {
            const valid = s.valid === false ? 'opacity:.4' : '';
            html += '<span style="' + valid + '">' + esc(s.attributeName) + ' ' + esc(s.attributeValue) + '</span>';
          });
          html += '</div>';
        }
        html += '</div>'; // 关闭 flex 内容容器
        html += '</div>'; // 关闭声骸卡片
      });
      html += '</div>'; // 关闭声骸列表
    }

    /* 如果什么都没解析到，显示原始数据 */
    if (!ral.length && !echoes.length && !wd.level) {
      html += '<div class="kuro-section-title">原始数据（调试用）</div>';
      html += '<pre style="white-space:pre-wrap;font-size:11px;color:var(--text2);background:rgba(0,0,0,0.2);padding:10px;border-radius:6px;max-height:400px;overflow:auto">' + this.escapeHtml(JSON.stringify(data, null, 2).slice(0, 5000)) + '</pre>';
    }

    return html;
  },

  renderDetailFromRaw(char, errorResp) {
    return this.renderDetail(char, char);
  },

  escapeHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  },
};
