# MingEchoAssistant_Final

鸣潮声骸词条解析、评分与推荐 + 云端多模态 AI 助手，**纯 C 后端 + Web 前端**，无 Python / Node 运行时依赖。

## 特性

- **智能助手（多模态）**：接入 **Agnes AI 云端模型**（`agnes-2.5-flash`，免费、多模态），支持 SSE 流式输出与图片输入，可直接粘贴游戏截图让模型看图回答（角色、面板、声骸词条分析）。云端推理，**不占用本机 CPU/GPU**，边打游戏边用也流畅。
- **声骸评分**：C 核心解析词条、按角色毕业线评分并给出养成推荐。
- **资源数据库**：C 导入器从 `resources/Project` 自动导入角色、声骸、武器、合鸣效果。
- **鸣潮风格深色 UI**：金色/冰青点缀的科幻面板风格，浏览器访问即用。

## 架构（纯 C + Web）

```text
MingEchoAssistant_Final
├── native/
│   ├── server/                  # C 后端：HTTP 服务器 + 路由 + 云端 SSE 代理
│   │   ├── main.c               # 入口（端口 8123，自动开浏览器）
│   │   ├── http_server.c        # 单进程多线程 HTTP 服务器
│   │   ├── routes.c             # REST API 路由（/api/*）
│   │   ├── mec_cloud.c          # curl.exe 子进程流式转发 Agnes AI（SSE）
│   │   ├── mec_config.c         # config.json / 资源读取（UTF-8）
│   │   └── win_utf8.h           # UTF-8 ↔ UTF-16 宽字符文件工具
│   ├── mec_core.c               # C 核心（导出）
│   ├── core/                    # 评分 / 推荐 / 面板解析
│   ├── parser/                  # 词条文本解析 + 中文词典
│   └── database/                # 资源加载 / 缓存 / 生成
├── web/                         # Web 前端（原生 HTML/CSS/JS，免构建）
│   ├── index.html
│   ├── css/style.css            # 鸣潮深色主题（金 --gold / 冰青 --ice）
│   └── js/{api,app,chat,score,db,status}.js
├── database/                    # 自动生成数据库（*.bin + catalog.json）
├── resources/Project/           # 已导入资源（角色立绘、声骸等）
├── resources/ui/                # 界面 Logo、图标
├── config.json                  # 云端模型（API 地址/密钥/模型）与对话配置
├── build_gcc.bat                # 一键构建：导入器 → 导入数据库 → 服务器
├── build/MingEchoServer.exe     # C 后端可执行文件（release）
└── run.bat                      # 启动（双击即用）
```

## 配置（config.json）

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

- `cloud.api_key`：**必填**，Agnes AI 的 API 密钥。
- `cloud.model`：模型名，默认 `agnes-2.5-flash`。
- `cloud.base_url`：API 地址，默认 `https://apihub.agnes-ai.com/v1`。
- `cloud.auto_start`：启动时是否自动检测云端连接。
- `chat.max_tokens`：单次回复最大 token 数，可适当调大。

## 构建（纯 C）

Windows + MinGW-w64 GCC（`C:\mingw64\bin\gcc.exe`）：

```bat
build_gcc.bat
```

三步：编译 `mec_import.exe` → 重新导入数据库 → 编译 `MingEchoServer.exe`（release，链接 `-lws2_32 -lwinhttp -lshell32 -lkernel32 -luser32 -lgdi32 -lm`）。

## 运行

```bat
run.bat
```

或：

```bat
build\MingEchoServer.exe --port 8123
```

启动后自动打开浏览器访问 `http://127.0.0.1:8123`，端口可用 `--port` 指定。

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

## 使用提示

- 在「智能助手」页，可直接 `Ctrl+V` 粘贴游戏截图，云端模型会看图回答面板与词条问题（替代旧版 OCR）。
- 在「声骸评分」页选择角色（可搜索下拉）、粘贴面板与声骸词条，点「评分 / 推荐」。
- 在「运行状态」页可「检测连接」云端模型，并查看 C 组件与资源库状态。
- 若界面样式未更新，按 `Ctrl+F5` 强制刷新（前端已带版本号防缓存）。

## 角色目标面板（毕业线）如何修改

### 声骸评分页「目标面板」的展示（Markdown 表格）

展示数据保存在 **`database/character_targets.json`**（由 `角色面板_修正版.md` 生成）。服务器**实时读取**，改完刷新页面立即生效，无需重启。

结构（每个角色一个对象）：

| 字段 | 含义 |
|---|---|
| `role` / `desc` | 定位（如 `主C`）与完整定位描述 |
| `stages` | 毕业阶段列表：`{label, cells}`，`cells` 键为 `crit/crit_damage/attack/energy/hp/def` |
| `statOrder` | 表格列顺序（决定显示哪些列） |
| `priority` | 属性优先级 |
| `weapon` / `echoes` / `cost` | 武器 / 声骸 / COST 配装建议 |

修改示例——把今汐大毕业暴击调到 78：

```json
"今汐": { "stages": [ ... { "label": "大毕业", "cells": { "crit": "≥78%", ... } } ] }
```

> 注意：修改 `角色面板_修正版.md` 后需要重新生成 json 才会生效；直接改 json 最快。

### 评分用目标面板（CSV，供"评分/养成推荐"计算）

评分逻辑读取 **`database/character_profiles.csv`**（列：`name,role,crit,crit_damage,attack,energy,basic,heavy,skill,liberation,attack_weight`），改完刷新即生效。当前 56 个角色已写入基线数值，可直接手动修正。

## 设计原则

- 后端零第三方依赖（仅链接 Windows 系统库；云端请求经系统自带 `curl.exe` 转发，规避本机 WinHTTP 被宿主钩子破坏的问题）。
- AI 走 Agnes AI 云端推理，不占用本机 CPU/GPU，不影响游戏帧率。
- 全程无 Python：界面为浏览器 Web 前端，后端为单文件 C 可执行程序。
