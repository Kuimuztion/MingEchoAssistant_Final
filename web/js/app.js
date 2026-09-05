/* app.js —— 路由、初始化、顶部徽章 */
const AppCfg = { system_prompt: '' };

/* URL路径 → 视图名 映射 */
const PATH_TO_VIEW = {
  '/': 'chat',
  '/skeleton': 'score',
  '/sql': 'db',
  '/runtime': 'status',
  '/account': 'game',
  '/character': 'chars',
  '/strategy': 'holo',
  '/tower': 'tower',
  '/project': 'about',
  '/endless': 'abyss',
  '/matrix': 'matrix',
};
/* 视图名 → URL路径 反向映射 */
const VIEW_TO_PATH = {};
for (const [p, v] of Object.entries(PATH_TO_VIEW)) VIEW_TO_PATH[v] = p;

/* 根据URL路径切换视图（页面加载和浏览器前进后退时调用） */
function applyRouteFromUrl() {
  const path = location.pathname;
  const view = PATH_TO_VIEW[path];
  if (view) {
    switchView(view, true); // skipPush=true，不重复pushState
    return true;
  }
  return false;
}

async function boot() {
  // 读取服务端状态，填充全局配置
  let hint = 'Agnes AI · agnes-2.5-flash · 云端多模态';
  let online = false;
  try {
    const st = await Api.json('/api/status');
    if (st.ok) {
      const c = st.cloud || {};
      AppCfg.system_prompt = st.system_prompt || '';
      online = !!c.has_key;
      hint = '云端多模态';
    }
  } catch (e) { /* 后端不可达 */ }

  const hintEl = document.getElementById('chatModelHint');
  if (hintEl) hintEl.textContent = hint;
  const nameEl = document.getElementById('chatModelName');
  if (nameEl) nameEl.textContent = '模型 ' + (hint.split(' · ')[0] || 'agnes-2.5-flash');

  const setChip = (chipId, on, txt) => {
    const el = document.getElementById(chipId);
    el.textContent = txt;
    el.className = 'chip ' + (on ? 'chip-on' : 'chip-off');
  };
  setChip('headerChip', online, online ? '模型在线' : '模型未连接');
  Chat.setOnline(online);

  // 导航路由
  document.querySelectorAll('.nav-btn').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
  });

  // 初始化各页面
  await Promise.allSettled([Chat.init(hint), Score.init(), Db.init(), Status.init(), Game.init(), Holo.init(), Tower.init()]);

  /* 用库街区首个角色的游戏头像作为用户头像 */
  if (typeof Chat !== 'undefined' && Chat.updateUserAvatar) Chat.updateUserAvatar();

  // 优先按URL路径路由，其次按 ?view= 参数
  if (!applyRouteFromUrl()) {
    const qv = new URLSearchParams(location.search).get('view');
    if (qv && ['chat', 'score', 'db', 'status', 'game', 'chars', 'holo', 'tower', 'abyss', 'matrix', 'about'].includes(qv)) switchView(qv);
  }
}

/* 切换视图，同时更新URL（skipPush=true时不更新URL，用于浏览器前进后退回调） */
function switchView(name, skipPush) {
  document.querySelectorAll('.nav-btn').forEach((b) => b.classList.toggle('active', b.dataset.view === name));
  document.querySelectorAll('.view').forEach((v) => v.classList.toggle('hidden', v.id !== 'view-' + name));
  if (!skipPush) {
    const path = VIEW_TO_PATH[name] || '/';
    if (location.pathname !== path) {
      history.pushState({ view: name }, '', path);
    }
  }
}

/* 浏览器前进后退 */
window.addEventListener('popstate', () => {
  applyRouteFromUrl();
});

window.addEventListener('DOMContentLoaded', boot);
