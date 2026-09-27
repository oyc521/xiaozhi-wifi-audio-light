#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
preview_lines.py — 从插画提取单色线稿并缩到目标尺寸，多方案对比。
用法: python scripts/preview_lines.py <图片路径>
"""
import sys, os
from PIL import Image, ImageOps, ImageFilter

def otsu(gray):
    h = gray.histogram(); total = sum(h)
    sa = sum(i*h[i] for i in range(256))
    wB = 0; sB = 0; best = 0; thr = 128
    for t in range(256):
        wB += h[t]
        if wB == 0: continue
        wF = total - wB
        if wF == 0: break
        sB += t*h[t]
        mB = sB/wB; mF = (sa-sB)/wF
        v = wB*wF*(mB-mF)**2
        if v > best: best = v; thr = t
    return thr

def head(src, frac=0.62):
    im = src.convert("RGBA")
    b = im.split()[3].getbbox()
    if b: im = im.crop(b)
    w, h = im.size
    im = im.crop((0, 0, w, int(h*frac)))
    w, h = im.size; s = max(w, h)
    c = Image.new("RGBA", (s, s), (255, 255, 255, 255))
    c.paste(im, ((s-w)//2, (s-h)//2), im)
    return c.convert("L")

def mono(im):
    return im.point(lambda v: 0 if v < 128 else 255)

def tile(im, size, scale):
    return im.resize((size*scale, size*scale), Image.NEAREST)

def main():
    src = Image.open(sys.argv[1])
    base = head(src)
    S = 4
    variants = []

    # A: 直接 40 BOX + Otsu
    a = base.resize((40, 40), Image.BOX); variants.append(("A 40 BOX otsu", mono(a.point(lambda v: 255 if v > otsu(a) else 0))))
    # B: 80 -> CONTOUR -> 40
    b = base.resize((80, 80), Image.LANCZOS).filter(ImageFilter.CONTOUR).resize((40, 40), Image.BOX)
    variants.append(("B contour40", mono(b)))
    # C: 80 -> FIND_EDGES -> 40
    c = base.resize((80, 80), Image.LANCZOS).filter(ImageFilter.FIND_EDGES).resize((40, 40), Image.BOX)
    variants.append(("C edges40", mono(c)))
    # D: 40 LANCZOS autocontrast otsu
    d = ImageOps.autocontrast(base.resize((40, 40), Image.LANCZOS))
    variants.append(("D 40 ac otsu", mono(d.point(lambda v: 255 if v > otsu(d) else 0))))
    # E: 64 contour (keep 64)
    e = base.resize((64, 64), Image.LANCZOS).filter(ImageFilter.CONTOUR)
    variants.append(("E contour64", mono(e)))
    # F: 80 blur2 contour -> 40
    f = base.resize((80, 80), Image.LANCZOS).filter(ImageFilter.GaussianBlur(2)).filter(ImageFilter.CONTOUR).resize((40, 40), Image.BOX)
    variants.append(("F blur contour40", mono(f)))

    tiles = [(n, tile(im, 40 if im.size[0] <= 40 else 64, S)) for n, im in variants]
    # 统一缩放到相同显示高度
    maxh = max(t.height for _, t in tiles)
    W = sum(t.width for _, t in tiles) + 10*(len(tiles)+1)
    out = Image.new("L", (W, maxh+20), 255)
    x = 10
    for _, t in tiles:
        out.paste(t, (x, 10)); x += t.width + 10
    d = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "faces_preview", "_lines.png")
    os.makedirs(os.path.dirname(d), exist_ok=True); out.save(d)
    print("order:", " | ".join(n for n, _ in variants)); print("saved", d)

if __name__ == "__main__":
    main()
