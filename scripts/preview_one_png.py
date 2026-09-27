#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
preview_one_png.py — 把任意 PNG 转成 NxN 单色像素脸并预览（不写入固件）。

用法: python scripts/preview_one_png.py <图片路径> [尺寸...]
例:   python scripts/preview_one_png.py scripts/faces_src/xx.png 40 32
输出: build/faces_preview/_try.png  （左:原图 右:各尺寸转换结果）
"""
import sys, os
from PIL import Image

def to_grid(im, size):
    im = im.convert("RGBA")
    bbox = im.split()[3].getbbox()
    if bbox:
        im = im.crop(bbox)
    w, h = im.size
    s = max(w, h)
    canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    canvas.paste(im, ((s - w) // 2, (s - h) // 2))
    im = canvas.resize((size, size), Image.LANCZOS)
    px = im.load()
    g = [[0] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            r, gg, b, a = px[x, y]
            fg = (a >= 128) and (0.299 * r + 0.587 * gg + 0.114 * b < 128)
            g[y][x] = 255 if fg else 0
    return g

def to_img(grid, scale):
    n = len(grid)
    im = Image.new("L", (n, n))
    px = im.load()
    for y in range(n):
        for x in range(n):
            px[x, y] = 0 if grid[y][x] else 255
    return im.resize((n * scale, n * scale), Image.NEAREST)

def main():
    path = sys.argv[1]
    sizes = [int(a) for a in sys.argv[2:]] or [40]
    src = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", src.size, (255, 255, 255, 255))
    flat = Image.alpha_composite(bg, src).convert("L")
    prev = flat.copy(); prev.thumbnail((300, 300))
    tiles = [prev]
    for s in sizes:
        tiles.append(to_img(to_grid(src, s), max(1, 300 // s)))
    pad = 12
    W = sum(t.width for t in tiles) + pad * (len(tiles) + 1)
    H = max(t.height for t in tiles) + 2 * pad
    out = Image.new("L", (W, H), 255)
    x = pad
    for t in tiles:
        out.paste(t, (x, pad)); x += t.width + pad
    dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "build", "faces_preview", "_try.png")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    out.save(dst)
    print("saved", dst, "sizes:", sizes)

if __name__ == "__main__":
    main()
