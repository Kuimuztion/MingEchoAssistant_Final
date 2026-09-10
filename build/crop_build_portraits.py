# -*- coding: utf-8 -*-
"""
为「配队养成」页面生成专用人物立绘。
- 自动裁掉四周透明留白，头顶贴顶、以头部质心水平居中（同 crop_team_portraits.py）
- 只保留上半身（头顶~腰/裙摆），适配养成页竖版视口比例 190:460
- 输出 2 倍分辨率(380x920)，前端一次性采样，最清晰
输入: resources/Project/角色/<名>.webp
输出: resources/Project/角色养成/<名>.webp
重复运行会覆盖，可对新增角色随时补生成。
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "resources", "Project", "角色")
CARD_DIR = os.path.join(ROOT, "resources", "Project", "角色卡片")
DST_DIR = os.path.join(ROOT, "resources", "Project", "角色养成")

AR = 190 / 460           # 养成页立绘视口宽高比
OUT_W, OUT_H = 380, 920  # 2x 高清

# 个别角色水平微调(正值=角色在视口中向右移)，与配队卡脚本保持一致
TUNE = {"爱弥斯": 0.22}
# 从卡片立绘取竖条时的水平微调(正值=竖条窗口左移、角色在框内右移)
SLICE_TUNE = {"爱弥斯": 0.18, "秧秧·玄翎": 0.08, "清霄": -0.10, "露帕": 0.25}


def crop_from_card(dst_dir, name, dst):
    """优先从配队卡片立绘(已按头部质心调好构图)取竖条，
    保证养成页人物位置与角色配队页卡片一致。"""
    src = os.path.join(dst_dir, name + ".webp")
    if not os.path.exists(src):
        return False
    im = Image.open(src).convert("RGBA")
    w, h = im.size
    sw = min(w, round(h * AR))
    left = (w - sw) // 2 - round(SLICE_TUNE.get(name, 0) * sw)
    left = max(0, min(left, w - sw))
    c = im.crop((left, 0, left + sw, h)).resize((OUT_W, OUT_H), Image.LANCZOS)
    c.save(dst, "WEBP", quality=92, method=6)
    return True


def head_center_x(im, l, t, r, b):
    """取内容顶部 18% 高度带（头部区域）的 alpha 加权水平质心。"""
    hb = max(1, b - t)
    band = im.crop((0, t, im.width, min(im.height, t + int(hb * 0.18) + 1)))
    sw = min(band.width, 256)
    band = band.resize((sw, 1))
    data = list(band.split()[3].getdata())
    total = sum(data)
    if not total:
        return (l + r) / 2
    scale = im.width / sw
    return (sum(i * a for i, a in enumerate(data)) / total) * scale


def crop_one(src, dst, name=None):
    name = name or os.path.splitext(os.path.basename(src))[0]
    if crop_from_card(CARD_DIR, name, dst):
        return True
    im = Image.open(src).convert("RGBA")
    W, H = im.size
    bbox = im.getbbox()
    if not bbox:
        return False
    l, t, r, b = bbox
    hb = b - t
    cx = head_center_x(im, l, t, r, b)
    lo = l + (r - l) * 0.2
    hi = l + (r - l) * 0.8
    cx = max(lo, min(cx, hi))

    # 只取上半身：以角色总高的 52% 作为视口高(头顶~腰/裙摆)，按竖版比例反推宽
    UPPER = 0.52
    ch = hb * UPPER
    cw = ch * AR
    top = t - ch * 0.03     # 头顶留 3%
    if cw > W:              # 图不够宽则以宽定高(裁上下)
        cw = W
        ch = cw / AR
    name = os.path.splitext(os.path.basename(src))[0]
    left = cx - cw / 2 - TUNE.get(name, 0) * cw
    left = max(0, min(left, W - cw))
    top = max(0, top)
    cw = min(cw, W - left)
    ch = min(ch, H - top)

    box = (round(left), round(top), round(left + cw), round(top + ch))
    c = im.crop(box).resize((OUT_W, OUT_H), Image.LANCZOS)
    c.save(dst, "WEBP", quality=92, method=6)
    return True


def main():
    os.makedirs(DST_DIR, exist_ok=True)
    n = 0
    for fn in os.listdir(SRC_DIR):
        if not fn.lower().endswith(".webp"):
            continue
        name = os.path.splitext(fn)[0]
        src = os.path.join(SRC_DIR, fn)
        dst = os.path.join(DST_DIR, name + ".webp")
        try:
            if crop_one(src, dst):
                n += 1
        except Exception as e:
            print("[skip]", name, e)
    print("done, generated", n, "portraits ->", DST_DIR)


if __name__ == "__main__":
    main()
