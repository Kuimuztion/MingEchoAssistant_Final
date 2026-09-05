/* status.js —— 运行状态页 */
const Status = {
  async init() {
    document.getElementById('cloudTestBtn').addEventListener('click', () => this.testCloud());
    document.getElementById('cloudRefreshBtn').addEventListener('click', () => this.refresh());
    document.getElementById('apiKeyEditBtn').addEventListener('click', () => this.toggleApiKeyEditor(true));
    document.getElementById('apiKeyCancelBtn').addEventListener('click', () => this.toggleApiKeyEditor(false));
    document.getElementById('apiKeySaveBtn').addEventListener('click', () => this.saveApiKey());
    await this.refresh();
  },

  toggleApiKeyEditor(show) {
    const ed = document.getElementById('apiKeyEditor');
    ed.style.display = show ? 'block' : 'none';
    if (show) {
      document.getElementById('apiKeyInput').value = '';
      document.getElementById('apiKeyMsg').textContent = '';
      document.getElementById('apiKeyInput').focus();
    }
  },

  async saveApiKey() {
    const key = document.getElementById('apiKeyInput').value.trim();
    const msg = document.getElementById('apiKeyMsg');
    const btn = document.getElementById('apiKeySaveBtn');
    if (!key) { msg.textContent = '秘钥不能为空'; msg.style.color = 'var(--red)'; return; }
    btn.disabled = true;
    btn.textContent = '保存中…';
    msg.textContent = '正在写入 config.json…';
    msg.style.color = '';
    try {
      const data = await Api.json('/api/config/api_key', { api_key: key });
      if (data.ok) {
        msg.textContent = '✓ ' + (data.message || '已保存');
        msg.style.color = 'var(--green)';
        document.getElementById('apiKeyInput').value = '';
        await this.refresh();
        setTimeout(() => this.toggleApiKeyEditor(false), 1500);
      } else {
        msg.textContent = '✕ ' + (data.error || '保存失败');
        msg.style.color = 'var(--red)';
      }
    } catch (e) {
      msg.textContent = '✕ ' + String(e);
      msg.style.color = 'var(--red)';
    } finally {
      btn.disabled = false;
      btn.textContent = '保存';
    }
  },

  async refresh() {
    const data = await Api.json('/api/status');
    if (!data.ok) {
      document.getElementById('cloudState').textContent = '服务异常';
      document.getElementById('cloudDetail').textContent = String(data.error || '无法连接后端');
      return;
    }
    const c = data.cloud || {};
    const hasKey = !!c.has_key;
    document.getElementById('cloudState').textContent = hasKey ? '● 已配置密钥' : '○ 未配置密钥';
    document.getElementById('cloudDetail').textContent =
      c.model + ' · ' + c.base_url + (hasKey ? ' · 已填写 API 密钥' : ' · 请点击「编辑API秘钥」填写');
    document.getElementById('cloudDetail').style.color = hasKey ? '' : 'var(--red)';

    // 组件检查
    const setComp = (id, ok, txt) => {
      const el = document.getElementById(id);
      el.textContent = txt;
      el.className = ok ? 'comp-ok' : 'comp-miss';
    };
    setComp('compCore', true, data.version || '-');
    setComp('compChars', (data.counts && data.counts.characters) > 0, '就绪 (' + (data.counts && data.counts.characters) + ')');
    setComp('compEchoes', (data.counts && data.counts.echoes) > 0, '就绪 (' + (data.counts && data.counts.echoes) + ')');
    setComp('compWeapons', (data.counts && data.counts.weapons) > 0, '就绪 (' + (data.counts && data.counts.weapons) + ')');
    setComp('compCloud', hasKey, hasKey ? '就绪' : '缺失');

    const counts = data.counts || {};
    document.getElementById('dbInfo').textContent =
      '角色 ' + counts.characters + ' · 声骸 ' + counts.echoes + ' · 武器 ' + counts.weapons +
      ' · 合鸣效果 ' + counts.resonance + ' · 合鸣词条 ' + counts.resonance_echoes;
    return data;
  },

  async testCloud() {
    const btn = document.getElementById('cloudTestBtn');
    btn.disabled = true;
    btn.textContent = '检测中…';
    document.getElementById('cloudDetail').textContent = '正在向云端发送测试请求…';
    try {
      const data = await Api.json('/api/status');
      const hasKey = data.cloud && data.cloud.has_key;
      if (!hasKey) {
        document.getElementById('cloudDetail').textContent = '未配置 API 密钥，无法检测。';
        return;
      }
      // 发一个极短对话验证云端连通
      let ok = false;
      let err = '';
      const handle = Api.stream('/api/chat', { messages: [{ role: 'user', content: 'ping' }] }, () => { ok = true; }, () => { if (!ok) ok = true; }, (e) => { err = e; });
      await new Promise((resolve) => setTimeout(resolve, 8000));
      handle.cancel();
      document.getElementById('cloudDetail').textContent = ok
        ? '✓ 云端连接正常：' + data.cloud.model
        : '✕ 连接异常：' + (err || '超时');
      document.getElementById('cloudDetail').style.color = ok ? 'var(--green)' : 'var(--red)';
    } finally {
      btn.disabled = false;
      btn.textContent = '检测连接';
    }
  },
};
