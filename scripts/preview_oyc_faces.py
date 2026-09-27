#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
preview_oyc_faces.py — 把生成器的像素脸导出为 PNG（黑脸白底，放大），便于肉眼查看。

输出到 <repo>/build/faces_preview/ :
  - <name>.png / <name>_blink.png   单张（放大 8x）
  - _sheet.png                      总览（4 列，每格 睁眼|闭眼 并排，放大 4x）
复用 gen_oyc_faces.py 的绘制函数，无需第三方库。
"""
import os, zlib, struct, importlib.util, math

def load_gen():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_oyc_faces.py")
    spec = importlib.util.spec_from_file_location("oyc_gen", path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m

def png_gray(path, grid):
    h = len(grid); w = len(grid[0])
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(grid[y])
    comp = zlib.compress(bytes(raw), 9)
    def chunk(typ, data):
        return struct.pack(">I", len(data)) + typ + data + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", comp))
        f.write(chunk(b"IEND", b""))

def scale(grid, s):
    out = []
    for row in grid:
        r = []
        for v in row:
            r.extend([v] * s)
        for _ in range(s):
            out.append(r)
    return out

def to_gray(grid):
    # 前景(255)->黑(0)，背景(0)->白(255)
    return [[0 if v else 255 for v in row] for row in grid]

def blank(w, h, v=255):
    return [[v] * w for _ in range(h)]

def paste(dst, src, ox, oy):
    for y in range(len(src)):
        for x in range(len(src[0])):
            if 0 <= oy + y < len(dst) and 0 <= ox + x < len(dst[0]):
                dst[oy + y][ox + x] = src[y][x]

def main():
    g = load_gen()
    W, H = g.W, g.H
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(root, "build", "faces_preview")
    os.makedirs(outdir, exist_ok=True)

    items = []  # (name, open_grid, blink_grid)
    for name, (eyes, extras) in g.FACES.items():
        items.append((name, g.build(eyes, extras, 64), g.build_blink(extras, 64)))

    # 单张（放大 8x）
    for name, go, gb in items:
        png_gray(os.path.join(outdir, "%s.png" % name), scale(to_gray(go), 8))
        png_gray(os.path.join(outdir, "%s_blink.png" % name), scale(to_gray(gb), 8))

    # 总览：4 列网格，每格 睁眼|闭眼 并排，放大 4x
    S = 4
    cols = 4
    rows = math.ceil(len(items) / cols)
    cell_w = (W * 2 + 4) * S      # open+blink + pad
    cell_h = H * S
    pad = 6
    sheet_w = cols * cell_w + (cols + 1) * pad
    sheet_h = rows * cell_h + (rows + 1) * pad
    sheet = blank(sheet_w, sheet_h, 255)
    for idx, (name, go, gb) in enumerate(items):
        r = idx // cols; c = idx % cols
        ox = pad + c * (cell_w + pad)
        oy = pad + r * (cell_h + pad)
        paste(sheet, scale(to_gray(go), S), ox, oy)
        paste(sheet, scale(to_gray(gb), S), ox + W * S + 2 * S, oy)
    png_gray(os.path.join(outdir, "_sheet.png"), sheet)

    print("faces:", ", ".join(n for n, _, _ in items))
    print("sheet:", os.path.join(outdir, "_sheet.png"))

if __name__ == "__main__":
    main()
