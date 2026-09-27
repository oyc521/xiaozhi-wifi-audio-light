#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_xiaoai_faces.py — 用 scripts/faces_src/小爱闭眼版.png 生成表情帧并覆盖到 oyc_faces.c。

- 闭眼帧 = 该图裁边 -> 目标尺寸(BOX) -> 二值。
- 睁眼帧 = 在闭眼帧上清掉眼睛区域、画上圆眼。
默认只覆盖 neutral；加 --all 则所有情绪都用小爱。
目标尺寸取自 gen_oyc_faces.py 的 W（大脸模式为 64）。
"""
import os, sys, importlib.util
from PIL import Image, ImageOps

SRC_OPEN = "小爱睁眼版.png"     # 睁眼帧来源（灰阶插画 -> 阈值）
SRC_CLOSE = "小爱闭眼版.png"    # 闭眼帧来源（纯黑白）
THR_OPEN = 215                  # 睁眼版阈值（灰阶插画用高阈值抠线）
THR_CLOSE = 128                 # 闭眼版阈值（纯黑白）

def load_gen():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_oyc_faces.py")
    spec = importlib.util.spec_from_file_location("oyc_gen", p)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m

def grid_from_png(path, size, thr):
    im = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
    g = Image.alpha_composite(bg, im).convert("L")
    g = g.crop(ImageOps.invert(g).getbbox())
    w, h = g.size; s = max(w, h)
    c = Image.new("L", (s, s), 255); c.paste(g, ((s - w) // 2, (s - h) // 2))
    BOX = Image.Resampling.BOX if hasattr(Image, "Resampling") else Image.BOX
    sm = c.resize((size, size), BOX)
    return [[255 if sm.getpixel((x, y)) < thr else 0 for x in range(size)] for y in range(size)]

# 在睁眼帧 (64x64 坐标系) 上清掉眼睛、画闭眼弧 -> 闭眼帧
EYE_L = (20, 48)   # 左眼中心(64系)
EYE_R = (41, 48)   # 右眼中心(64系)
EYE_HALF = 6       # 闭眼弧半宽
EYE_DROP = 2       # 下弯量
EYE_THICK = 2      # 线粗

def _seg(m, x0, y0, x1, y1, t, size):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        x = round(x0 + (x1 - x0) * i / n)
        y = round(y0 + (y1 - y0) * i / n)
        for dy in range(t):
            for dx in range(t):
                xx, yy = x + dx, y + dy
                if 0 <= xx < size and 0 <= yy < size:
                    m[yy][xx] = 255

def derive_gtlt(open_grid, size):
    """在小爱睁眼帧上把眼睛改成 > < （开心眯眼）。"""
    s = size / 64.0
    q = lambda v: int(round(v * s))
    m = [row[:] for row in open_grid]
    for y in range(q(45), q(54)):
        for x in range(q(12), q(28)): m[y][x] = 0
        for x in range(q(34), q(49)): m[y][x] = 0
    d = max(2, q(6)); t = max(1, q(2)); cy = q(48); lx = q(20); rx = q(41)
    _seg(m, lx - d, cy - d, lx, cy, t, size); _seg(m, lx, cy, lx - d, cy + d, t, size)   # >
    _seg(m, rx + d, cy - d, rx, cy, t, size); _seg(m, rx, cy, rx + d, cy + d, t, size)   # <
    return m

def derive_closed(open_grid, size):
    s = size / 64.0
    q = lambda v: int(round(v * s))
    m = [row[:] for row in open_grid]
    # 清眼区（左右分开）
    for y in range(q(45), q(54)):
        for x in range(q(12), q(28)): m[y][x] = 0
        for x in range(q(34), q(49)): m[y][x] = 0
    half, drop, thick = max(1, q(EYE_HALF)), max(1, q(EYE_DROP)), max(1, q(EYE_THICK))
    for (cx, cy) in (EYE_L, EYE_R):
        cx, cy = q(cx), q(cy)
        for i in range(-half, half + 1):
            t = abs(i) / float(half)
            y = cy + int(round(drop * (1 - t)))
            for dy in range(thick):
                if 0 <= cx + i < size and 0 <= y + dy < size:
                    m[y + dy][cx + i] = 255
    return m

def main():
    gen = load_gen()
    size = gen.W
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    src_open = os.path.join(here, "faces_src", SRC_OPEN)
    src_close = os.path.join(here, "faces_src", SRC_CLOSE)
    if not os.path.exists(src_open):
        print("缺少图:", src_open); return
    out = os.path.join(root, "main", "boards", "xiaozhi-wifi-audio-light", "oyc_faces.c")

    src_happy = os.path.join(here, "faces_src", SRC_CLOSE)
    def open_fn(s):  return grid_from_png(src_open, s, THR_OPEN)
    def close_fn(s): return derive_closed(open_fn(s), s)   # 从睁眼帧改出闭眼帧（眨眼用）
    def happy_fn(s): return grid_from_png(src_happy, s, THR_CLOSE)

    if "--all" in sys.argv:
        overrides = {name: (open_fn, close_fn) for name in gen.FACES}
        print("覆盖: 全部情绪 -> 小爱")
    else:
        overrides = {"neutral": (open_fn, close_fn)}
        print("覆盖: neutral -> 小爱 (--all 可全替换)")
    overrides["happy"] = (happy_fn, happy_fn)
    print("happy -> %s" % SRC_CLOSE)

    gen.generate(out, overrides)
    print("wrote", out)
    import subprocess
    subprocess.run([sys.executable, os.path.join(here, "render_faces_c.py")], check=False)

if __name__ == "__main__":
    main()
