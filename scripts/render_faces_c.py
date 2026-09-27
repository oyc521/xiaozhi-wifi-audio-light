#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
render_faces_c.py — 直接解析 main/boards/.../oyc_faces.c 渲染预览（保证与固件一致）。
输出: build/faces_preview/_sheet_c.png
"""
import os, re, sys, zlib, struct

ORDER = ["neutral","happy","laughing","loving","cool","sad","crying","angry",
         "surprised","sleepy","thinking","confused","winking"]

def parse(path):
    txt = open(path, encoding="utf-8").read()
    mm = re.search(r"\.w = (\d+), \.h = (\d+)", txt)
    size = int(mm.group(1)) if mm else 40
    maps = {}
    for m in re.finditer(r"static const uint8_t (oyc_face_\w+_map)\[\]\s*=\s*\{(.*?)\};", txt, re.S):
        name = m.group(1)
        nums = [int(v) for v in re.findall(r"\d+", m.group(2))]
        if len(nums) >= size*size:
            maps[name] = nums[:size*size]
    return maps, size

def to_img(nums, scale, size):
    n = size
    im = [[0 if nums[y*n+x] else 255 for x in range(n)] for y in range(n)]
    out = []
    for row in im:
        r = []
        for v in row: r.extend([v]*scale)
        for _ in range(scale): out.append(r)
    return out

def png_gray(path, grid):
    h=len(grid); w=len(grid[0]); raw=bytearray()
    for row in grid: raw.append(0); raw.extend(row)
    comp=zlib.compress(bytes(raw),9)
    def chunk(t,d): return struct.pack(">I",len(d))+t+d+struct.pack(">I",zlib.crc32(t+d)&0xffffffff)
    with open(path,"wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,0,0,0,0)))
        f.write(chunk(b"IDAT",comp)); f.write(chunk(b"IEND",b""))

def main():
    root=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cpath=os.path.join(root,"main","boards","xiaozhi-wifi-audio-light","oyc_faces.c")
    maps,size=parse(cpath)
    S = 4 if size <= 40 else 3
    pad=6; cell=(size*2+4)*S; cellh=size*S
    cols=4; rows=(len(ORDER)+cols-1)//cols
    W=cols*cell+(cols+1)*pad; H=rows*cellh+(rows+1)*pad
    sheet=[[255]*W for _ in range(H)]
    def paste(g,ox,oy):
        for y,row in enumerate(g):
            for x,v in enumerate(row):
                if 0<=oy+y<H and 0<=ox+x<W: sheet[oy+y][ox+x]=v
    for i,name in enumerate(ORDER):
        r=i//cols; c=i%cols
        ox=pad+c*(cell+pad); oy=pad+r*(cellh+pad)
        op=maps.get("oyc_face_%s_map"%name); bl=maps.get("oyc_face_%s_blink_map"%name)
        if op: paste(to_img(op,S,size), ox, oy)
        if bl: paste(to_img(bl,S,size), ox+size*S+2*S, oy)
    d=os.path.join(root,"build","faces_preview","_sheet_c.png")
    os.makedirs(os.path.dirname(d),exist_ok=True); png_gray(d,sheet)
    print("size", size, "maps:", len(maps), "->", d)

if __name__=="__main__":
    main()
