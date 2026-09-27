#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_oyc_faces.py — 生成氛围灯 OLED 的 40x40 A8 像素表情（C 数组）。

无第三方依赖：内部位图光栅绘制，输出 C 到
  main/boards/xiaozhi-wifi-audio-light/oyc_faces.c
公开接口（供 oled_display.cc 调用）:
  const lv_image_dsc_t* oyc_face_get(const char* emotion, bool blink);

A8(alpha): 前景=255(显示为黑)，背景=0(透明→白)。
每个表情两张：睁眼(open) 与 闭眼(blink)。
"""
import math, os

W = H = 64          # 目标位图尺寸（大脸模式 64x64）
REF = 40.0          # 造型参考坐标系（原 40x40）
S = W / REF
FG = 255

def new():
    return [[0] * W for _ in range(H)]

def px(g, x, y, v=FG):
    if 0 <= x < W and 0 <= y < H:
        g[y][x] = v

def q(v):
    return int(round(v * S))

def pxs(g, x, y, v=FG):     # 用 40 坐标写单点（高光等）
    px(g, q(x), q(y), v)

def disc(g, cx, cy, r, v=FG):
    cx, cy, r = q(cx), q(cy), r * S
    for y in range(H):
        for x in range(W):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px(g, x, y, v)

def ring(g, cx, cy, r, t=2, v=FG):
    cx, cy, r, t = q(cx), q(cy), r * S, t * S
    for y in range(H):
        for x in range(W):
            if r - t / 2.0 <= math.hypot(x - cx, y - cy) <= r + t / 2.0:
                px(g, x, y, v)

def arc(g, cx, cy, r, a0, a1, t=2, v=FG):
    cx, cy, r, t = q(cx), q(cy), r * S, t * S
    for y in range(H):
        for x in range(W):
            if abs(math.hypot(x - cx, y - cy) - r) <= t / 2.0:
                if a0 <= math.degrees(math.atan2(y - cy, x - cx)) % 360 <= a1:
                    px(g, x, y, v)

def hline(g, x0, x1, y, t=1, v=FG):
    x0, x1, y, t = q(x0), q(x1), q(y), max(1, q(t))
    for x in range(min(x0, x1), max(x0, x1) + 1):
        for dy in range(t):
            px(g, x, y + dy, v)

def line(g, x0, y0, x1, y1, t=1, v=FG):
    x0, y0, x1, y1, t = q(x0), q(y0), q(x1), q(y1), max(1, q(t))
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        x = round(x0 + (x1 - x0) * i / n)
        y = round(y0 + (y1 - y0) * i / n)
        for dy in range(t):
            px(g, x, y + dy, v)

# ---------------- 部件 ----------------
def head(g, r=18):
    ring(g, 20, 20, r, 2)

# 眼睛（open）
def eyes_dots(g, ex=13, ey=16, r=5):
    for cx in (ex, 40 - ex):
        disc(g, cx, ey, r)
        for dx in (-2, -1):
            for dy in (-2, -1):
                pxs(g, cx + dx, ey + dy, 0)   # 2x2 高光
def eyes_dots3(g): eyes_dots(g, r=4)
def eyes_dots5(g): eyes_dots(g, r=6)
def eyes_happy(g):
    for cx in (13, 27):
        arc(g, cx, 18, 5, 200, 340, 2)
HEART = [
    ".##.##.",
    "#######",
    "#######",
    ".#####.",
    "..###..",
    "...#...",
]
def stamp(g, pat, ox, oy):
    for j, row in enumerate(pat):
        for i, ch in enumerate(row):
            if ch == '#':
                x0, x1 = q(ox + i), q(ox + i + 1)
                y0, y1 = q(oy + j), q(oy + j + 1)
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        px(g, x, y)
def eyes_heart(g):
    for cx in (13, 27):
        stamp(g, HEART, cx - 3, 13)
def eyes_confused(g):
    disc(g, 13, 17, 4); pxs(g, 11, 15, 0)
    disc(g, 27, 17, 2)
def eyes_wink(g):
    disc(g, 13, 17, 4); pxs(g, 11, 15, 0)
    hline(g, 23, 31, 17, 2)

# 闭眼（blink）
def eyes_closed(g):
    for cx in (13, 27):
        hline(g, cx - 4, cx + 4, 17, 2)

# 其它部件
def mouth_smile(g, a0=35, a1=145, r=7, t=2): arc(g, 20, 21, r, a0, a1, t)
def mouth_big(g):   arc(g, 20, 20, 8, 25, 155, 3)
def mouth_frown(g): arc(g, 20, 29, 7, 215, 325, 2)
def mouth_o(g):     ring(g, 20, 25, 3, 2)
def mouth_line(g, y=25, x0=16, x1=24): hline(g, x0, x1, y, 2)
def smile_one(g):   arc(g, 23, 23, 6, 300, 60, 2)
def blush(g):
    for cx in (7, 33):
        hline(g, cx - 2, cx + 2, 23, 2)   # 腮红短横
def tear(g):        disc(g, 10, 24, 2); pxs(g, 10, 27, FG)
def brows_angry(g):
    line(g, 8, 11, 16, 14, 2); line(g, 32, 11, 24, 14, 2)

# 每个表情 = (open_eyes, [extras...])；blink 用 eyes_closed 替换 open_eyes
FACES = {
    "neutral":   (eyes_dots,   [mouth_smile]),
    "happy":     (eyes_dots3,  [mouth_big, blush]),
    "laughing":  (eyes_happy,  [mouth_big, blush]),
    "loving":    (eyes_heart,  [mouth_smile, blush]),
    "cool":      (eyes_dots,   [smile_one]),
    "sad":       (eyes_dots,   [mouth_frown]),
    "crying":    (eyes_dots,   [mouth_frown, tear]),
    "angry":     (eyes_dots3,  [brows_angry, mouth_frown]),
    "surprised": (eyes_dots5,  [mouth_o]),
    "sleepy":    (eyes_closed, [lambda g: mouth_line(g, 27, 17, 23)]),
    "thinking":  (eyes_dots,   [lambda g: mouth_line(g, 27, 18, 26)]),
    "confused":  (eyes_confused, [lambda g: mouth_smile(g, 60, 120)]),
    "winking":   (eyes_wink,   [mouth_smile]),
}

ALIASES = {
    "funny": "laughing", "silly": "laughing", "delicious": "laughing",
    "relaxed": "happy", "delighted": "happy", "confident": "cool",
    "embarrassed": "loving", "kissy": "loving", "shocked": "surprised",
}

def set_size(size):
    global W, H, S
    W = H = size
    S = size / REF

def build(eyes, extras, size):
    set_size(size)
    g = new(); head(g)
    for fn in extras: fn(g)
    eyes(g)
    return g

def build_blink(extras, size):
    set_size(size)
    g = new(); head(g)
    for fn in extras: fn(g)
    eyes_closed(g)
    return g

def emit(name, g, size):
    data = []
    for y in range(size): data.extend(g[y])
    L = ["static const uint8_t %s_map[] = {" % name]
    row = []
    for v in data:
        row.append("%d," % v)
        if len(row) == 40:
            L.append("    " + "".join(row)); row = []
    if row: L.append("    " + "".join(row))
    L.append("};")
    L.append("static const lv_image_dsc_t %s = {" % name)
    L.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0,")
    L.append("                .w = %d, .h = %d, .stride = %d, .reserved_2 = 0 }," % (size, size, size))
    L.append("    .data_size = sizeof(%s_map)," % name)
    L.append("    .data = %s_map," % name)
    L.append("};")
    return "\n".join(L)

def preview():
    for name, (eyes, extras) in FACES.items():
        print("=== %s ===" % name)
        g = build(eyes, extras, 64)
        gb = build_blink(extras, 64)
        for y in range(64):
            print("".join("#" if g[y][x] else "." for x in range(64)) + "   " +
                  "".join("#" if gb[y][x] else "." for x in range(64)))

SIZES = (64, 48)   # 大脸 / 小脸（状态栏下）

def generate(out, overrides=None):
    """overrides: {name: (open_fn, blink_fn)}，fn(size)->grid；用于外部图片覆盖某表情。"""
    overrides = overrides or {}
    parts = ['#include "oyc_faces.h"\n']
    tables = {}
    for size in SIZES:
        tbl = []
        for name, (eyes, extras) in FACES.items():
            if name in overrides:
                ofn, bfn = overrides[name]
                go, gb = ofn(size), bfn(size)
            else:
                go, gb = build(eyes, extras, size), build_blink(extras, size)
            suffix = "" if size == 64 else "_s"
            sn = "oyc_face_%s%s" % (name, suffix)
            parts.append(emit(sn, go, size))
            parts.append(emit(sn + "_blink", gb, size))
            tbl.append('    { "%s", &%s, &%s_blink },' % (name, sn, sn))
        tables[size] = tbl
    alias_rows = ['    { "%s", "%s" },' % (a, b) for a, b in ALIASES.items()]
    parts.append(
        "typedef struct { const char* name; const lv_image_dsc_t* open; const lv_image_dsc_t* blink; } oyc_face_entry_t;\n"
        "static const oyc_face_entry_t s_faces64[] = {\n" + "\n".join(tables[64]) + "\n};\n"
        "static const oyc_face_entry_t s_faces48[] = {\n" + "\n".join(tables[48]) + "\n};\n"
        "typedef struct { const char* alias; const char* name; } oyc_face_alias_t;\n"
        "static const oyc_face_alias_t s_aliases[] = {\n" + "\n".join(alias_rows) + "\n};\n"
        "const lv_image_dsc_t* oyc_face_get(const char* emotion, bool blink, bool big) {\n"
        "    const char* name = emotion ? emotion : \"neutral\";\n"
        "    for (size_t i = 0; i < sizeof(s_aliases)/sizeof(s_aliases[0]); i++)\n"
        "        if (strcmp(name, s_aliases[i].alias) == 0) { name = s_aliases[i].name; break; }\n"
        "    const oyc_face_entry_t* t = big ? s_faces64 : s_faces48;\n"
        "    size_t n = big ? sizeof(s_faces64)/sizeof(s_faces64[0]) : sizeof(s_faces48)/sizeof(s_faces48[0]);\n"
        "    for (size_t i = 0; i < n; i++)\n"
        "        if (strcmp(name, t[i].name) == 0) return blink ? t[i].blink : t[i].open;\n"
        "    return blink ? t[0].blink : t[0].open;\n"
        "}\n"
    )
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))
    return out

def main():
    import sys
    if "--preview" in sys.argv:
        preview(); return
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "main", "boards", "xiaozhi-wifi-audio-light", "oyc_faces.c")
    generate(out)
    print("wrote", out)

if __name__ == "__main__":
    main()
