/* mec_gacha.h - 抽卡记录：游戏日志解析 + 官方接口同步（纯 C + 系统 curl，零第三方依赖）
 *
 * 实现参考开源项目 juliy819/wuwa-gacha-tool (Apache-2.0)：
 *   1. 鸣潮 Client.log 为加密日志：跳过前 3 字节 BOM，逐字节 XOR 解密（奇数字节 ^0xA5，偶数字节 ^0xEF）
 *   2. 解密后在 "OpenWebView ... sdkJson" 行中提取官方唤取记录链接
 *      （host = aki-gm-resources.aki-game.com，path = /aki/gacha/index.html，fragment 以 /record 开头）
 *   3. 链接参数（player_id / record_id / resources_id / svr_id / lang）作为凭据，
 *      按 13 种卡池类型逐个 POST 到 gmserver-api.aki-game2(.com|.net)/gacha/record/query，
 *      每次返回该卡池全量记录（无分页），合并后落盘 gacha_data.json
 */
#ifndef MEC_GACHA_H
#define MEC_GACHA_H

/* 扫描游戏安装目录下的 Client.log，提取最新一条唤取记录链接。
 * game_dir 为 UTF-8 路径，可为空（空则使用 gacha_config.json 记住的目录）。
 * 成功返回 0 并把完整链接写入 out_url；失败返回 -1 并把原因写入 err。 */
int mec_gacha_scan(const char* root, const char* game_dir, char* out_url, int out_size, char* err, int err_size);

/* 从唤取记录链接解析参数，逐个卡池同步官方记录，合并写入 <root>/gacha_data.json。
 * url 为用户扫描得到或手动粘贴的完整链接。
 * 成功返回 0，out 输出摘要 JSON（同步了哪些卡池、记录总数）；失败返回 -1，out 输出错误 JSON。 */
int mec_gacha_sync(const char* root, const char* url, char* out, int out_size);

#endif
