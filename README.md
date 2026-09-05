# MingEchoAssistant_Final

鸣潮角色面板评分、声骸评分、游戏账号数据浏览 + 云端多模态 AI 助手，**纯 C 后端 + Web 前端**，无 Python / Node 运行时依赖。

## 特性

- **智能助手（多模态）**：接入 **Agnes AI 云端模型**（`agnes-2.5-flash`，免费、多模态），支持 SSE 流式输出与图片输入，可直接粘贴游戏截图让模型看图回答。云端推理，**不占用本机 CPU/GPU**，边打游戏边用也流畅。
- **角色评分**：独立评分页面，基于角色目标面板（小毕业/大毕业/伤害加成范围/有效词条目标）对角色主面板、武器、共鸣链、声骸进行本地算法综合评分，支持**保存到本机截图**。
- **声骸评分**：C 核心解析词条、按角色毕业线评分。
- **游戏账户（库街区连接）**：支持短信验证码登录 / Token 登录，连接库街区账号后可查看玩家已拥有的角色列表、角色详情（主面板、声骸、武器、共鸣链）。
- **角色信息**：浏览玩家所有角色，点击角色查看完整面板和声骸配置，可直接跳转角色评分。
- **全息战略**：查看演武、同步、幻痛、强袭四类挑战的 Boss 全息 1~6 级通关记录与通关时间。
- **逆境深塔**：查看残响之塔、深境之塔、回音之塔的通关层数与出战队伍角色。
- **无尽湍渊 / 终焉矩阵**：查看真实分数与出战队伍角色头像。
- **资源数据库**：浏览角色、声骸、武器、合鸣效果、合鸣词条，声骸与武器带立绘图标。
- **运行状态**：检测云端 AI 连接、编辑 API 秘钥、查看 C 组件与资源库状态。
- **关于此项目**：开源声明、致谢、数据隐私说明、GPL-3.0 协议。
- **鸣潮风格深色 UI**：金色 / 冰青点缀的科幻面板风格，浏览器访问即用，完整页面路由（每个功能独立 URL）。

## 架构（纯 C + Web）

```text
MingEchoAssistant_Final
├── native/
│   ├── server/                  # C 后端：HTTP 服务器 + 路由 + 云端 SSE 代理 + 库街区 API
│   │   ├── main.c               # 入口（端口 8123，自动开浏览器，UTF-8 控制台）
│   │   ├── http_server.c        # 单进程多线程 HTTP 服务器
│   │   ├── routes.c             # REST API 路由（/api/*）+ SPA 路由回退
│   │   ├── mec_cloud.c          # curl.exe 子进程流式转发 Agnes AI（SSE）
│   │   ├── mec_kuro.c           # 库街区 API 代理（curl 子进程）+ 账号配置 + 请求频率限制
│   │   ├── mec_config.c         # config.json / 资源读取（UTF-8）
│   │   ├── import_main.c        # 资源导入器（编译时用）
│   │   └── win_utf8.h           # UTF-8 ↔ UTF-16 宽字符文件工具
│   ├── mec_core.c               # C 核心（导出）
│   ├── core/                    # 评分 / 推荐 / 面板解析
│   ├── parser/                  # 词条文本解析 + 中文词典
│   └── database/                # 资源加载 / 缓存 / 生成
├── web/                         # Web 前端（原生 HTML/CSS/JS，免构建）
│   ├── index.html               # 主应用（智能助手 / 声骸评分 / 数据库 / 运行状态 / 游戏账户 / 角色信息 / 全息战略 / 逆境深塔 / 无尽湍渊 / 终焉矩阵 / 关于此项目）
│   ├── char_score.html          # 独立角色评分页面（新标签页打开，支持保存截图）
│   ├── gpl3.html                # GPL-3.0 完整协议页面
│   ├── css/style.css            # 鸣潮深色主题（金 --gold / 冰青 --ice）
│   ├── js/                      # app.js（路由）/ chat.js / score.js / db.js / status.js / game.js / html2canvas.min.js
│   └── character_panels.json    # 角色目标面板数据（评分用，由 resources/Role/角色面板.md 生成）
├── database/                    # 自动生成数据库（*.bin + catalog.json + character_panels.json）
├── resources/
│   ├── Project/                 # 角色头像、立绘、声骸图标、武器图标
│   ├── Role/角色面板.md         # 角色目标面板源文件（小毕业/大毕业/伤害加成范围/有效词条目标）
│   ├── Icons/                   # 致谢图标（Agnes AI / Claude / ChatGPT / 库街区 / 豆包）
│   └── User/                    # AI.png / User.png（智能助手头像）
├── config.json                  # 云端模型（API 地址/密钥/模型）与对话配置
├── kuro_config.json             # 库街区账号配置（token / role_id / server_id，连接后自动生成）
├── parse_panels.py              # 角色面板解析脚本（开发工具，运行时不需要 Python）
├── build_gcc.bat                # 一键构建：导入器 → 导入数据库 → 服务器
├── build/MingEchoServer.exe     # C 后端可执行文件（release）
├── run.bat                      # 启动（双击即用）
├── LICENSE                      # GNU GPL v3.0
└── README.md
```

## 前端页面路由

| URL | 页面 |
|---|---|
| `http://127.0.0.1:8123/` | 智能助手 |
| `/skeleton` | 声骸评分 |
| `/sql` | 数据库 |
| `/runtime` | 运行状态 |
| `/account` | 游戏账户 |
| `/character` | 角色信息 |
| `/strategy` | 全息战略 |
| `/tower` | 逆境深塔 |
| `/endless` | 无尽湍渊 |
| `/matrix` | 终焉矩阵 |
| `/project` | 关于此项目 |
| `/char_score.html?roleId=...&serverId=...&charId=...&name=...` | 独立角色评分页面（新标签页） |

## 配置

### config.json（云端 AI）

```json
{
  "cloud": {
    "base_url": "https://apihub.agnes-ai.com/v1",
    "model": "agnes-2.5-flash",
    "api_key": "sk-xxxx",
    "auto_start": true,
    "timeout": 90
  },
  "chat": { "max_tokens": 4096, "temperature": 0.7, "system_prompt": "..." }
}
```

- `cloud.api_key`：**必填**，Agnes AI 的 API 密钥。也可在「运行状态」页面点击「编辑API秘钥」填写，新密钥会覆盖旧密钥。
- `cloud.model`：模型名，默认 `agnes-2.5-flash`。
- `cloud.base_url`：API 地址，默认 `https://apihub.agnes-ai.com/v1`。

### kuro_config.json（库街区账号，连接后自动生成）

```json
{
  "token": "eyJhbGciOi...",
  "role_id": "109290098",
  "server_id": "76402e5b20be2c39f095a152090afddc"
}
```

- `token`：库街区长期登录 Token（短信登录后自动获取，或手动填写）。
- `role_id`：游戏内角色 UID。
- `server_id`：区服 ID，国服固定为 `76402e5b20be2c39f095a152090afddc`，界面中设为只读，如需其他区服请手动编辑此文件。

> 所有账号数据仅保存在本地 `kuro_config.json`，**不上传任何服务器**。

## 构建（纯 C）

Windows + MinGW-w64 GCC（需支持 C11，将 `gcc.exe` 加入 PATH）：

```bat
build_gcc.bat
```

三步：编译 `mec_import.exe` → 重新导入数据库 → 编译 `MingEchoServer.exe`（release，链接 `-lws2_32 -lwinhttp -lshell32 -lkernel32 -luser32 -lgdi32 -lm`）。

**编译依赖**：仅需 gcc（MinGW-w64）+ Windows 系统库（自带）。不需要 Python、Node 或任何第三方库。

## 运行

```bat
run.bat
```

或：

```bat
build\MingEchoServer.exe --port 8123
```

启动后自动打开浏览器访问 `http://127.0.0.1:8123`，端口可用 `--port` 指定。

**运行依赖**：
- Windows 10 1803+（系统自带 `C:\Windows\System32\curl.exe`，项目通过 curl 子进程转发 HTTPS 请求）
- 网络连接（访问 Agnes AI 与库街区 API）
- 浏览器（Chrome / Edge / Firefox）

> 普通用户使用已编译的 `MingEchoServer.exe` 即可，**不需要 gcc，不需要 Python**。

## API

| 端点 | 说明 |
|---|---|
| `GET /api/status` | 云端配置、密钥、资源库计数 |
| `GET /api/characters` | 角色列表 |
| `GET /api/database` | 声骸 / 武器 / 合鸣效果 / 合鸣词条 |
| `POST /api/score` | 评分（`character` + `echo_text` + `panel_text`） |
| `POST /api/recommend` | 养成推荐 |
| `POST /api/parse` | 词条文本解析 |
| `POST /api/chat` | 云端对话（SSE 流式，支持多模态图片） |
| `POST /api/kuro/sms` | 库街区短信验证码请求 |
| `POST /api/kuro/login` | 库街区短信登录 / Token 登录 |
| `GET /api/kuro/roles` | 获取玩家角色列表 |
| `GET /api/kuro/role_detail` | 获取角色详情（面板 / 声骸 / 武器 / 共鸣链） |
| `GET /api/kuro/challenge` | 获取全息战略挑战记录 |
| `GET /api/kuro/tower` | 获取逆境深塔记录 |
| `GET /api/kuro/abyss` | 获取无尽湍渊 / 终焉矩阵记录 |

## 角色目标面板（毕业线）

角色目标面板数据保存在 **`web/character_panels.json`** 与 **`database/character_panels.json`**，由 `resources/Role/角色面板.md` 解析生成。

每个角色包含：
- `small_graduate` / `big_graduate`：小毕业 / 大毕业核心属性（暴击 / 暴击伤害 / 攻击 / 共鸣效率 / 生命 / 防御 等）
- `small_damage_bonuses` / `big_damage_bonuses`：伤害加成范围（如热熔伤害加成 45%~60%、重击伤害加成 12%~20%）
- `valid_keywords`：有效词条目标（声骸副词条中哪些属性算"优质"，用于声骸评分）
- `priority`：属性优先级
- `weapons`：武器推荐优先级
- `echo_set` / `cost4` / `cost3` / `cost1`：声骸套装与 COST 配装建议

### 修改方法

1. 直接编辑 `resources/Role/角色面板.md`
2. 运行 `python parse_panels.py`（开发工具，仅此时需要 Python）
3. 脚本自动同步更新 `database/character_panels.json` 和 `web/character_panels.json`
4. 刷新浏览器页面即生效，无需重启服务器

> 评分算法根据每个角色的目标面板动态构建评分维度：主 C 评暴击 / 暴伤 / 攻击 / 共鸣效率，奶妈只评攻击 / 共鸣效率 / 生命，不评暴击暴伤。声骸评分的副词条"优质"判断也基于该角色的 `valid_keywords`。

## 使用提示

- **智能助手**：可直接 `Ctrl+V` 粘贴游戏截图，云端模型会看图回答面板与词条问题。
- **游戏账户**：首次使用需连接库街区账号。推荐在库街区官方 App 获取验证码后输入；也可手动填写 Token。区服 ID 国服固定，界面只读。
- **角色信息**：连接账号后显示玩家所有角色，点击角色查看完整面板、声骸、武器、共鸣链，点击「评分」跳转到独立评分页面。
- **角色评分**：评分页面在新标签页打开，点击「保存到本机」可将整个评分长页面截图为 PNG 保存到本地；点击「返回角色列表」关闭当前标签页回到角色信息。
- **声骸评分**：选择角色、粘贴面板与声骸词条，点「评分」。
- **运行状态**：可「检测连接」云端模型、「编辑API秘钥」。
- **关于此项目**：查看开源声明、致谢、数据隐私说明，点击底部许可协议可查看完整 GPL-3.0 文本。
- 若界面样式未更新，按 `Ctrl+F5` 强制刷新（前端已带版本号防缓存）。

## 数据隐私与安全

- **所有数据仅保存在本地**：库街区账号 Token、角色 UID、API 秘钥均保存在本地 `kuro_config.json` 和 `config.json`，**不上传任何第三方服务器**。
- **请求频率限制**：对库街区 API 的请求有 1 秒间隔限制，避免对官方服务器造成压力。
- **临时文件**：运行时产生的临时文件路由到 Windows 系统缓存目录（`%temp%`），可通过 `Win+R` 输入 `%temp%` 清理。
- **开源透明**：本项目包括所有源代码完全开源免费，接受社区审查。

## 开源协议

本项目采用 **GNU General Public License v3.0 (GPL-3.0)** 开源协议。

- 允许自由使用、修改、分发，但修改后的衍生作品必须同样以 GPL-3.0 开源并发布源代码。
- 禁止将本项目代码用于闭源商业产品。
- 其他版本的此项目（包括但不限于功能、权限、数据来源等）与主干开发者无关，不承担任何法律纠纷。
- 请自行按需修改自己想要的版本，禁止恶意攻击他人服务器和恶意篡改。

完整协议文本见 [LICENSE](LICENSE) 或运行后访问 `http://127.0.0.1:8123/gpl3.html`。

## 免责声明

1. **非官方产品**：本项目为玩家自发开发的第三方工具，**不隶属于库洛游戏，与库洛游戏无任何官方关联**。鸣潮（Wuthering Waves）游戏名称、角色、声骸、武器、美术资源等所有知识产权均归库洛游戏（Kuro Games）所有。
2. **仅供学习交流**：本项目仅供个人学习、研究与交流使用，**不得用于任何商业用途**，包括但不限于出售、出租、嵌入商业产品等。
3. **使用风险自担**：使用本项目所产生的任何直接或间接后果（包括但不限于账号封禁、数据丢失、游戏异常等），均由用户自行承担，**项目开发者不承担任何法律责任**。
4. **衍生版本免责**：本项目的其他分支、修改版、衍生版本（包括但不限于功能变更、权限调整、数据来源替换等）与主干开发者无关，**主干开发者不承担任何法律纠纷**。请自行辨别版本来源，按需修改自己想要的版本。
5. **禁止恶意使用**：禁止利用本项目进行恶意攻击他人服务器、恶意篡改数据、侵犯他人隐私、违反库洛游戏用户协议等行为。如因此产生法律后果，由使用者自行承担。
6. **数据来源说明**：玩家角色数据通过库街区公开 API 获取，仅在本地展示与评分，**不上传任何第三方服务器**。如库街区 API 政策变更导致功能失效，项目开发者不承担责任。
7. **AI 生成内容**：智能助手功能由 Agnes AI 云端模型生成内容，AI 输出仅供参考，不构成任何游戏建议或投资建议。AI 生成内容的准确性与完整性由模型提供方负责。

## 致谢

- **库街区**：提供游戏角色图鉴与玩家数据 API。
- **Agnes AI**：提供免费云端多模态模型 API。
- 鸣潮游戏及其角色、声骸、武器等美术资源版权归库洛游戏所有。

## 设计原则

- 后端零第三方依赖（仅链接 Windows 系统库；云端请求经系统自带 `curl.exe` 转发，规避本机 WinHTTP 被宿主钩子破坏的问题）。
- AI 走 Agnes AI 云端推理，不占用本机 CPU/GPU，不影响游戏帧率。
- 全程无 Python 运行时：界面为浏览器 Web 前端，后端为单文件 C 可执行程序。`parse_panels.py` 仅为开发时解析角色面板的工具，运行时不需要。
- 评分算法完全本地运行，不依赖云端 AI，保证评分一致性与可复现性。
