/* team.js —— 角色配队：主C角色选择网格 */
(function () {
  /* 主C角色列表（爱弥斯置顶，其余按角色面板顺序） */
  const MAIN_C_LIST = [
    '爱弥斯', '绯雪', '秧秧·玄翎', '清霄', '露西', '西格莉卡', '陆·赫斯',
    '嘉贝莉娜', '奥古斯塔', '弗洛洛', '卡提希娅', '赞妮', '珂莱塔', '椿',
    '长离', '今汐', '忌炎'
  ];

  /* 元素映射（用于头像边框颜色） */
  const ELEMENT_COLOR = {
    '热熔': '#e8643c', '冷凝': '#5ab8e8', '衍射': '#e8c44c',
    '湮灭': '#a878e8', '气动': '#5ce8a8', '导电': '#c8a0e8'
  };

  function renderTeamMains() {
    const grid = document.getElementById('teamMainGrid');
    if (!grid) return;
    let html = '';
    for (const name of MAIN_C_LIST) {
      const avatar = '/res/Project/角色头像/' + encodeURIComponent(name) + '.png';
      html += '<div class="team-main-card" onclick="openTeamPage(\'' + name.replace(/'/g, "\\'") + '\')" title="' + name + '">';
      html += '<div class="team-main-avatar"><img src="' + avatar + '" onerror="this.src=\'/res/ui/app.png\'" alt="' + name + '"></div>';
      html += '<div class="team-main-name">' + name + '</div>';
      html += '</div>';
    }
    grid.innerHTML = html;
  }

  /* 跳转到配队详情页（新标签页） */
  window.openTeamPage = function (name) {
    const url = '/team.html?main=' + encodeURIComponent(name);
    window.open(url, '_blank');
  };

  /* 暴露初始化函数，供 app.js 视图切换时调用 */
  window.initTeamView = renderTeamMains;

  document.addEventListener('DOMContentLoaded', renderTeamMains);
})();
