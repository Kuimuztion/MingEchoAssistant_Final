# -*- coding: utf-8 -*-
"""
为「角色配队」竖版卡片生成专用立绘。
- 自动裁掉四周透明留白
- 头顶贴近视口顶部、以头部质心水平居中
  （头发大幅侧飘的立绘，整图包围盒中心会被飘发拉离身体，导致角色被挤到椭圆一侧）
- 统一成椭圆视口比例 286:380，只保留上半身~大腿
- 输出 2 倍分辨率(572x760)，前端一次性采样，最清晰
输入: resources/Project/角色/<名>.webp
输出: resources/Project/角色卡片/<名>.webp
重复运行会覆盖，可对新增角色随时补生成。
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "resources", "Project", "角色")
DST_DIR = os.path.join(ROOT, "resources", "Project", "角色卡片")

AR = 286 / 380          # 椭圆视口宽高比
OUT_W, OUT_H = 572, 760  # 2x 高清

# 个别角色水平微调(正值=角色在卡片中向右移)
TUNE = {"爱弥斯": 0.09, "露帕": 0.6, "洛可可": -0.3, "忌炎": 0.18}
# 个别角色垂直微调(正值=窗口下移，去掉头顶上方空白)
TUNE_V = {"露帕": 0.26, "洛可可": 0.3, "折枝": 0.32}
# 个别角色头部质心手动覆盖(绝对x像素)：整图无透明留白/手持道具贴顶时，
# 顶部18%带质心会被道具(如折枝的花枝)拉偏，需按人物面部手动指定
CX_OVERRIDE = {"折枝": 760}


def head_center_x(im, l, t, r, b):
    """取内容顶部 18% 高度带（头部区域）的 alpha 加权水平质心，
    比整图包围盒中心更贴近身体中轴，不受侧飘长发/裙摆干扰。"""
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


def crop_one(src, dst):
    im = Image.open(src).convert("RGBA")
    W, H = im.size
    bbox = im.getbbox()
    if not bbox:
        return False
    l, t, r, b = bbox
    hb = b - t
    cx = head_center_x(im, l, t, r, b)
    # 质心仍可能被单侧飘发拉动，限制在包围盒中部 60% 区域内
    lo = l + (r - l) * 0.2
    hi = l + (r - l) * 0.8
    cx = max(lo, min(cx, hi))
    name = os.path.splitext(os.path.basename(src))[0]
    if name in CX_OVERRIDE:
        cx = CX_OVERRIDE[name]

    # 只取上半身并放大：以角色总高的 52% 作为视口高(头顶~腰/裙摆)，再按椭圆比反推宽
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
    top = t - ch * 0.03 + TUNE_V.get(name, 0) * ch
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
