/* app.js —— 路由、初始化、顶部徽章 */
const AppCfg = { system_prompt: '' };

/* URL路径 → 视图名 映射 */
const PATH_TO_VIEW = {
  '/': 'game',
  '/sql': 'db',
  '/runtime': 'status',
  '/account': 'game',
  '/character': 'chars',
  '/strategy': 'holo',
  '/tower': 'tower',
  '/project': 'about',
  '/endless': 'abyss',
  '/matrix': 'matrix',
  '/team': 'team',
  '/gacha': 'gacha',
};
/* 视图名 → URL路径 反向映射 */
const VIEW_TO_PATH = {};
for (const [p, v] of Object.entries(PATH_TO_VIEW)) VIEW_TO_PATH[v] = p;

/* 各视图背景图（db/status 无专属背景，用默认渐变） */
const VIEW_BG = {
  game: '/res/Background/account.jpg',
  chars: '/res/Background/character.jpg',
  holo: '/res/Background/strategy.png',
  tower: '/res/Background/tower.png',
  abyss: '/res/Background/endless.png',
  matrix: '/res/Background/matrix.jpg',
  team: '/res/Background/team.png',
  about: '/res/Background/project.png',
  gacha: '/res/Background/gacha.png',
};

/* 按视图切换页面背景（加深色蒙层保证内容可读） */
function applyViewBackground(name) {
  const bg = VIEW_BG[name];
  const body = document.body.style;
  document.body.classList.toggle('has-view-bg', !!bg);
  if (bg) {
    body.backgroundImage = 'linear-gradient(rgba(10,13,19,.82),rgba(10,13,19,.82)),url("' + bg + '")';
    body.backgroundSize = 'cover';
    body.backgroundPosition = 'center top';
    body.backgroundAttachment = 'fixed';
    body.backgroundRepeat = 'no-repeat';
  } else {
    body.backgroundImage = '';
    body.backgroundSize = '';
    body.backgroundPosition = '';
    body.backgroundAttachment = '';
    body.backgroundRepeat = '';
  }
}

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
  /* 先按URL同步切换视图，避免被下方异步初始化的网络请求阻塞而先闪一下默认视图 */
  const routedEarly = applyRouteFromUrl();

  // 导航路由
  document.querySelectorAll('.nav-btn').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
  });

  // 初始化各页面
  await Promise.allSettled([Db.init(), Status.init(), Game.init(), Holo.init(), Tower.init()]);
  if (typeof window.initTeamView === 'function') window.initTeamView();
  Gacha.init();

  // 若启动时未按URL路径路由成功，再按 ?view= 参数兜底
  if (!routedEarly) {
    const qv = new URLSearchParams(location.search).get('view');
    if (qv && ['db', 'status', 'game', 'chars', 'holo', 'tower', 'abyss', 'matrix', 'team', 'gacha', 'about'].includes(qv)) switchView(qv);
  }
}

/* 切换视图，同时更新URL（skipPush=true时不更新URL，用于浏览器前进后退回调） */
function switchView(name, skipPush) {
  applyViewBackground(name);
  document.querySelectorAll('.nav-btn').forEach((b) => b.classList.toggle('active', b.dataset.view === name));
  document.querySelectorAll('.view').forEach((v) => v.classList.toggle('hidden', v.id !== 'view-' + name));
  if (name === 'team' && typeof window.initTeamView === 'function') window.initTeamView();
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

/* 禁止拖动图片资源（CSS 之外的 JS 兜底） */
document.addEventListener('dragstart', (e) => { if (e.target && e.target.tagName === 'IMG') e.preventDefault(); });
window.addEventListener('DOMContentLoaded', boot);
