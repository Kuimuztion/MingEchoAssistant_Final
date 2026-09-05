/* MingEchoServer.exe 入口：初始化 C 核心 -> 启动本地 Web 服务 -> 打开浏览器 */
#include "http_server.h"
#include "mec_config.h"
#include "mec_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void open_browser(int port) {
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

int main(int argc, char** argv) {
    /* 强制控制台使用 UTF-8 代码页，避免中文输出乱码 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    char root[2048];
    GetModuleFileNameA(NULL, root, sizeof(root));
    char* slash = strrchr(root, '\\');
    if (slash) *slash = 0;
    else snprintf(root, sizeof(root), ".");

    /* 若 exe 位于 build\ 子目录，项目根取其父目录（web/、database/、config.json 在项目根） */
    {
        const char* base = strrchr(root, '\\');
        base = base ? base + 1 : root;
        if (_stricmp(base, "build") == 0) {
            char* p = strrchr(root, '\\');
            if (p) *p = 0;
        }
    }

    int port = 8123;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            snprintf(root, sizeof(root), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) port = 8123;
        } else if (strcmp(argv[i], "--no-browser") == 0) {
            /* 由 run.bat 控制，默认总是打开；此参数保留 */
        }
    }

    printf("============================================\n");
    printf("  MingEchoAssistant Server  v1.2  (纯 C)\n");
    printf("============================================\n");
    printf("[core] 初始化 C 核心...\n");
    mec_init(root);
    printf("[core] 版本: %s\n", mec_version());
    printf("[core] 角色 %d / 声骸 %d / 武器 %d / 合鸣 %d / 合鸣词条 %d\n",
           mec_db_count_characters(), mec_db_count_echoes(), mec_db_count_weapons(),
           mec_db_count_resonance(), mec_db_count_resonance_echoes());

    MecCloudConfig cfg;
    mec_config_load(root, &cfg);
    printf("[cloud] 模型: %s\n", cfg.model);
    printf("[cloud] 密钥: %s\n", cfg.api_key[0] ? "已配置" : "未配置（请在 config.json 填写 cloud.api_key）");

    printf("[web]   页面目录: %s\\web\n", root);
    printf("[web]   打开浏览器: http://127.0.0.1:%d\n", port);
    printf("[info]  Ctrl+C 停止服务。\n");
    fflush(stdout);

    /* 服务端线程（默认自动开浏览器） */
    open_browser(port);
    int rc = http_server_start(root, port);
    if (rc != 0) {
        fprintf(stderr, "[error] %s\n", mec_last_error());
        return 1;
    }
    return 0;
}
