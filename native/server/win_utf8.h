/* win_utf8.h - Windows 下正确处理中文路径：UTF-8 字符串 <-> UTF-16 Win32 API */
#ifndef MEC_WIN_UTF8_H
#define MEC_WIN_UTF8_H

#include <stdio.h>
#include <windows.h>

/* UTF-8 -> UTF-16（含结尾 \\0）。返回写入字符数；失败 0 */
static int utf8_to_wide(const char* s, wchar_t* out, int out_chars) {
    if (!s || !out || out_chars <= 0) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, out_chars);
}

/* UTF-16 -> UTF-8。返回写入字节数；失败 0 */
static int wide_to_utf8(const wchar_t* s, char* out, int out_bytes) {
    if (!s || !out || out_bytes <= 0) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, s, -1, out, out_bytes, NULL, NULL);
}

/* 用 UTF-8 路径打开文件（内部转 UTF-16 走 _wfopen，正确处理中文） */
static FILE* mec_fopen_utf8(const char* path, const char* mode) {
    wchar_t wp[2048], wm[8];
    if (!utf8_to_wide(path, wp, 2048)) return NULL;
    if (!utf8_to_wide(mode, wm, 8)) return NULL;
    return _wfopen(wp, wm);
}

/* 用 UTF-8 路径 + UTF-8 内容写文件（覆盖写）。返回 0 成功 */
static int mec_write_file_utf8(const char* path, const char* data, size_t len) {
    FILE* f = mec_fopen_utf8(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len ? 0 : -1;
}

#endif
