#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
import_oyc_faces_png.py — 用外部像素图覆盖表情。

把 PNG 放到 scripts/faces_src/ 下，文件名用表情名：
    neutral.png / neutral_blink.png
  （可用名: neutral happy laughing loving cool sad crying angry
           surprised sleepy thinking confused winking）
规则：
- 建议 40x40、黑图案 / 白底（或透明底黑图案）。
- 只给 <name>.png          -> 睁眼=闭眼（不眨眼）
- 再给 <name>_blink.png    -> 该表情会眨眼
- 未提供的表情 -> 保留脚本生成版
运行：python scripts/import_oyc_faces_png.py
"""
import os, sys, importlib.util, subprocess

def load_gen():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_oyc_faces.py")
    spec = importlib.util.spec_from_file_location("oyc_gen", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m

def png_to_grid(path, size):
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    px = im.load()
    g = [[0] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            r, gg, b, a = px[x, y]
            if a < 128:
                fg = False
            else:
                fg = (0.299 * r + 0.587 * gg + 0.114 * b) < 128
            g[y][x] = 255 if fg else 0
    return g

def main():
    gen = load_gen()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    src = os.path.join(here, "faces_src")
    out = os.path.join(root, "main", "boards", "xiaozhi-wifi-audio-light", "oyc_faces.c")

    names = list(gen.FACES.keys())
    overrides, used = {}, []

    def loader(path):
        return lambda s: png_to_grid(path, s)

    if os.path.isdir(src):
        for name in names:
            op = os.path.join(src, "%s.png" % name)
            bl = os.path.join(src, "%s_blink.png" % name)
            if os.path.exists(op):
                ofn = loader(op)
                bfn = loader(bl) if os.path.exists(bl) else ofn
                overrides[name] = (ofn, bfn)
                used.append(name + ("+blink" if os.path.exists(bl) else ""))

    if not overrides:
        print("faces_src/ 里没找到 <name>.png，未覆盖任何表情。")
        print("可用表情名:", ", ".join(names))
        print("路径:", src)
        return

    gen.generate(out, overrides)
    print("overrode:", ", ".join(used))
    print("wrote", out)
    subprocess.run([sys.executable, os.path.join(here, "preview_oyc_faces.py")], check=False)

if __name__ == "__main__":
    main()
