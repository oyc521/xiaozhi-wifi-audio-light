#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
preview_variants.py — 把一张 PNG 用多种方案转成单色像素并排对比。

用法: python scripts/preview_variants.py <图片路径>
输出: build/faces_preview/_variants.png
方案: 40/64 尺寸 x 面积平均(BOX)/LANCZOS x 阈值; 以及 40 抖动; 以及裁剪脸部区域。
"""
import sys, os
from PIL import Image, ImageOps

def prep(im, crop_face=False):
    im = im.convert("RGBA")
    bbox = im.split()[3].getbbox()
    if bbox:
        im = im.crop(bbox)
    if crop_face:  # 只取上部（脸）区域
        w, h = im.size
        im = im.crop((0, 0, w, int(h * 0.66)))
    w, h = im.size
    s = max(w, h)
    canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    canvas.paste(im, ((s - w) // 2, (s - h) // 2))
    bg = Image.new("RGBA", canvas.size, (255, 255, 255, 255))
    return Image.alpha_composite(bg, canvas).convert("L")

def to_img(gray, size, method, mode):
    g = gray.resize((size, size), method)
    if mode == "thr":
        g = g.point(lambda v: 0 if v < 128 else 255)
        mono = Image.new("L", g.size)
        mono.putdata(list(g.getdata()))
    else:  # dither
        mono = g.convert("1", dither=Image.FLOYDSTEINBERG).convert("L")
    return mono

def main():
    path = sys.argv[1]
    src = Image.open(path)
    boxes = [
        ("40 Box thr",     prep(src),           40, Image.BOX,      "thr"),
        ("40 Lanczos thr", prep(src),           40, Image.LANCZOS,  "thr"),
        ("64 Box thr",     prep(src),           64, Image.BOX,      "thr"),
        ("64 Lanczos dith",prep(src),           64, Image.LANCZOS,  "dith"),
        ("40 face-crop",   prep(src, True),     40, Image.BOX,      "thr"),
        ("64 face-crop",   prep(src, True),     64, Image.BOX,      "thr"),
    ]
    tiles = []
    for name, gray, size, method, mode in boxes:
        im = to_img(gray, size, method, mode)
        sc = max(1, 240 // size)
        tiles.append((name, im.resize((size * sc, size * sc), Image.NEAREST)))

    pad = 10
    label_h = 0
    W = sum(t.width for _, t in tiles) + pad * (len(tiles) + 1)
    H = max(t.height for _, t in tiles) + 2 * pad
    out = Image.new("L", (W, H), 255)
    x = pad
    for _, t in tiles:
        out.paste(t, (x, pad)); x += t.width + pad
    dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "build", "faces_preview", "_variants.png")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    out.save(dst)
    print("order:", " | ".join(n for n, _ in tiles))
    print("saved", dst)

if __name__ == "__main__":
    main()
