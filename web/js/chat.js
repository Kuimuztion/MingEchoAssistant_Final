/* chat.js —— 智能助手页：多模态对话 + SSE 流式渲染 */
const Chat = {
  history: [],
  pendingImages: [],
  worker: null,
  currentBubble: null,
  currentReasoning: '',
  aiAvatar: '/res/Project/User/AI.png',
  userAvatar: '/res/Project/User/User.png',

  /* 更新用户头像（库街区连接后调用，用首个角色的游戏头像；只设置一次） */
  updateUserAvatar() {
    /* 如果已经设置过（非默认），不再更改，保证所有消息头像一致 */
    const saved = localStorage.getItem('chat_user_avatar');
    if (saved && saved !== '/res/Project/User/User.png') {
      this.userAvatar = saved;
      return;
    }
    if (Game.characters && Game.characters.length > 0) {
      const first = Game.characters[0];
      if (first.roleIconUrl) {
        this.userAvatar = first.roleIconUrl;
        localStorage.setItem('chat_user_avatar', first.roleIconUrl);
      }
    }
  },

  async init(modelHint) {
    const hint = document.getElementById('chatModelHint');
    if (hint) hint.textContent = modelHint || '云端多模态';

    document.getElementById('sendBtn').addEventListener('click', () => this.send());
    const clearBtn = document.getElementById('clearChatBtn');
    if (clearBtn) clearBtn.addEventListener('click', () => this.clear());
    document.getElementById('imgBtn').addEventListener('click', () => this.chooseImage());
    document.getElementById('stopBtn').addEventListener('click', () => { if (this.worker) this.worker.cancel(); });

    const input = document.getElementById('chatInput');
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); this.send(); }
    });
    input.addEventListener('paste', (e) => {
      const items = (e.clipboardData && e.clipboardData.items) || [];
      for (const it of items) {
        if (it.type && it.type.startsWith('image/')) {
          const file = it.getAsFile();
          if (file) { this.addImageFile(file); e.preventDefault(); return; }
        }
      }
    });
  },

  setOnline(online) {
    const chip = document.getElementById('chatChip');
    if (chip) {
      chip.textContent = online ? '模型在线' : '模型未连接';
      chip.className = 'chip ' + (online ? 'chip-on' : 'chip-off');
    }
  },

  chooseImage() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'image/*';
    input.onchange = () => { if (input.files[0]) this.addImageFile(input.files[0]); };
    input.click();
  },

  addImageFile(file) {
    const reader = new FileReader();
    reader.onload = () => {
      this.pendingImages.push(reader.result);
      this.renderChips();
    };
    reader.readAsDataURL(file);
  },

  renderChips() {
    const row = document.getElementById('chipsRow');
    row.innerHTML = '';
    this.pendingImages.forEach((img, i) => {
      const chip = document.createElement('span');
      chip.className = 'img-chip';
      chip.innerHTML = '图片 ' + (i + 1) + ' <span class="x">×</span>';
      chip.querySelector('.x').onclick = () => { this.pendingImages.splice(i, 1); this.renderChips(); };
      row.appendChild(chip);
    });
  },

  addMessage(role, name) {
    const list = document.getElementById('chatList');
    /* 外层容器：头像 + 气泡 */
    const wrap = document.createElement('div');
    wrap.className = 'msg-wrap ' + (role === 'user' ? 'msg-wrap-user' : 'msg-wrap-ai');
    /* 头像 */
    const avatar = document.createElement('div');
    avatar.className = 'msg-avatar';
    const avatarImg = document.createElement('img');
    avatarImg.src = role === 'user' ? this.userAvatar : this.aiAvatar;
    avatarImg.onerror = function() { this.style.display = 'none'; };
    avatar.appendChild(avatarImg);
    /* 气泡 */
    const box = document.createElement('div');
    box.className = 'msg ' + (role === 'user' ? 'msg-user' : 'msg-ai');
    /* 按顺序排列：AI 是头像左、气泡右；用户是气泡左、头像右 */
    if (role === 'user') {
      wrap.appendChild(box);
      wrap.appendChild(avatar);
    } else {
      wrap.appendChild(avatar);
      wrap.appendChild(box);
    }
    list.appendChild(wrap);
    document.getElementById('chatScroll').scrollTop = list.scrollHeight;
    return box;
  },

  send() {
    const input = document.getElementById('chatInput');
    const text = input.value.trim();
    const images = this.pendingImages.slice();
    if ((!text && images.length === 0) || (this.worker && this.currentBubble)) return;
    // 组装用户消息
    const content = text || '请查看图片。';
    const userParts = [];
    if (images.length) {
      userParts.push({ type: 'text', text: content });
      images.forEach((url) => userParts.push({ type: 'image_url', image_url: { url } }));
    }
    this.history.push({ role: 'user', text: content, images });

    const userBox = this.addMessage('user', '你');
    images.forEach((url) => {
      const img = document.createElement('img');
      img.className = 'msg-img';
      img.src = url;
      userBox.appendChild(img);
    });
    if (text) {
      const t = document.createElement('div');
      t.textContent = text;
      userBox.appendChild(t);
    }

    // 组装 messages（含 system）
    const messages = [];
    const sys = window.AppCfg && AppCfg.system_prompt;
    if (sys) messages.push({ role: 'system', content: sys });
    for (let i = 0; i < this.history.length - 1; i++) {
      const m = this.history[i];
      let c = m.text;
      if (m.images && m.images.length) {
        const parts = [{ type: 'text', text: c }];
        m.images.forEach((url) => parts.push({ type: 'image_url', image_url: { url } }));
        c = parts;
      }
      messages.push({ role: m.role, content: c });
    }
    // 当前消息
    let curContent = content;
    if (images.length) {
      const parts = [{ type: 'text', text: content }];
      images.forEach((url) => parts.push({ type: 'image_url', image_url: { url } }));
      curContent = parts;
    }
    messages.push({ role: 'user', content: curContent });

    input.value = '';
    this.pendingImages = [];
    this.renderChips();

    // 助手气泡
    const aiBox = this.addMessage('assistant', '鸣潮助手');
    /* 思考过程 toggle（放在最上面） */
    const reasonToggle = document.createElement('div');
    reasonToggle.className = 'msg-reason-toggle';
    reasonToggle.style.cursor = 'pointer';
    reasonToggle.textContent = '思考过程 ▾';
    reasonToggle.style.display = 'none';
    reasonToggle.style.fontSize = '11px';
    reasonToggle.style.color = 'var(--gold)';
    reasonToggle.style.marginBottom = '6px';
    aiBox.appendChild(reasonToggle);
    /* 回复文字 */
    const textDiv = document.createElement('div');
    textDiv.className = 'msg-text';
    aiBox.appendChild(textDiv);
    /* 思考过程内容 */
    const reasonDiv = document.createElement('div');
    reasonDiv.className = 'reason-block';
    reasonDiv.style.display = 'none';
    aiBox.appendChild(reasonDiv);
    reasonToggle.onclick = () => { reasonDiv.style.display = reasonDiv.style.display === 'none' ? '' : 'none'; };

    this.currentBubble = aiBox;
    this.currentReasoning = '';
    document.getElementById('sendBtn').disabled = true;
    document.getElementById('stopBtn').style.display = '';
    document.getElementById('chatScroll').scrollTop = document.getElementById('chatList').scrollHeight;

    this.worker = Api.stream('/api/chat', { messages }, (d) => {
      if (d.reasoning) {
        this.currentReasoning += d.reasoning;
        reasonDiv.textContent = normNewline(d.fullReasoning);
        if (d.fullReasoning && reasonToggle.style.display === 'none') reasonToggle.style.display = '';
      }
      if (d.content) {
        const cleaned = normNewline(d.fullContent).replace(/^\n+/, '');
        textDiv.textContent = cleaned;
        document.getElementById('chatScroll').scrollTop = document.getElementById('chatList').scrollHeight;
      }
    }, () => {
      this.finish();
    }, (err) => {
      this.finish();
      textDiv.textContent = '[请求失败：' + err + ']';
    });
  },

  finish() {
    document.getElementById('sendBtn').disabled = false;
    document.getElementById('stopBtn').style.display = 'none';
    this.currentBubble = null;
  },

  clear() {
    this.history = [];
    const list = document.getElementById('chatList');
    list.innerHTML = '';
    const sys = document.createElement('div');
    sys.className = 'msg system-msg';
    sys.textContent = '对话已清空。';
    list.appendChild(sys);
  },
};
