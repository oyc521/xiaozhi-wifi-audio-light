#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_xiaoai.py — 手绘 40x40 单色「小爱」像素脸（预览用，满意后再并入 oyc_faces）。

特征：浅色长发+齐刘海(中分小缺口) + 头顶小皇冠 + 闭眼弧线 + 小腮红/嘴。
用法: python scripts/gen_xiaoai.py [--png]
"""
import math, os, sys

W = H = 40
FG = 255

def new(): return [[0]*W for _ in range(H)]
def px(g,x,y,v=FG):
    if 0<=x<W and 0<=y<H: g[y][x]=v

def disc(g,cx,cy,r,v=FG):
    for y in range(H):
        for x in range(W):
            if (x-cx)**2+(y-cy)**2 <= r*r: px(g,x,y,v)

def ellipse(g,cx,cy,rx,ry,v=FG):
    for y in range(H):
        for x in range(W):
            if ((x-cx)/rx)**2+((y-cy)/ry)**2 <= 1: px(g,x,y,v)

def ring(g,cx,cy,r,t=1,v=FG):
    for y in range(H):
        for x in range(W):
            if r-t/2<=math.hypot(x-cx,y-cy)<=r+t/2: px(g,x,y,v)

def line(g,x0,y0,x1,y1,t=1,v=FG):
    n=int(max(abs(x1-x0),abs(y1-y0)))+1
    for i in range(n+1):
        x=round(x0+(x1-x0)*i/n); y=round(y0+(y1-y0)*i/n)
        for dy in range(t): px(g,x,y+dy,v)

def hline(g,x0,x1,y,t=1,v=FG):
    for x in range(min(x0,x1),max(x0,x1)+1):
        for dy in range(t): px(g,x,y+dy,v)

def blob(g,cx,cy,r):
    for y in range(cy-r,cy+r+1):
        for x in range(cx-r,cx+r+1):
            if (x-cx)**2+(y-cy)**2 <= r*r+1: px(g,x,y)

def tri(g, cx, base_y, h, w):
    for i in range(h):
        y = base_y - i
        half = round(w * (1 - i / float(h)) / 2)
        hline(g, cx - half, cx + half, y, 1)

def crown(g):
    # 底座 + 三个尖（中尖略高）
    hline(g, 12, 28, 12, 1)
    tri(g, 14, 12, 5, 6)
    tri(g, 20, 12, 8, 7)
    tri(g, 26, 12, 5, 6)

def draw(eyes="closed", mouth="smile"):
    g = new()
    # 头发：钟形轮廓
    ellipse(g, 20, 20, 14, 13)
    ellipse(g, 7, 27, 4, 8)          # 左发束
    ellipse(g, 33, 27, 4, 8)         # 右发束
    # 脖子/衣领
    hline(g, 16, 24, 35, 3)
    # 脸（挖白，尽量大）
    ellipse(g, 20, 25, 9, 8, 0)
    # 齐刘海：仅盖额头上沿，中分留缺口
    for y in range(17, 22):
        for x in range(W):
            if ((x - 20) / 9.3) ** 2 + ((y - 25) / 8.3) ** 2 <= 1:
                if not (abs(x - 20) <= 1 and y <= 19):
                    px(g, x, y, FG)
    # 皇冠（尖端高出头顶）
    crown(g)
    # 眼睛（闭眼短横 / 睁眼圆点）
    if eyes == "closed":
        hline(g, 12, 16, 25, 2); hline(g, 24, 28, 25, 2)
    else:
        blob(g, 14, 25, 2); blob(g, 26, 25, 2)
    # 嘴（∪ 形微笑 / 张开 / 平）
    if mouth == "open":
        ellipse(g, 20, 30, 2, 2, FG); ellipse(g, 20, 30, 1, 1, 0)
    elif mouth == "flat":
        hline(g, 18, 22, 30, 1)
    else:
        px(g, 18, 30, FG); px(g, 19, 31, FG); px(g, 20, 31, FG); px(g, 21, 31, FG); px(g, 22, 30, FG)
    return g

def ascii(g, label):
    print("=== %s ===" % label)
    for y in range(H): print("".join("#" if g[y][x] else "." for x in range(W)))

def to_png(grid, path, scale=8):
    import zlib, struct
    n=len(grid); im=[]
    for y in range(n):
        row=[0]*(n*scale)
        for x in range(n):
            v=0 if grid[y][x] else 255
            for k in range(scale): row[x*scale+k]=v
        for _ in range(scale): im.append(row)
    h=len(im); w=len(im[0]); raw=bytearray()
    for row in im: raw.append(0); raw.extend(row)
    comp=zlib.compress(bytes(raw),9)
    def chunk(t,d): return struct.pack(">I",len(d))+t+d+struct.pack(">I",zlib.crc32(t+d)&0xffffffff)
    with open(path,"wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,0,0,0,0)))
        f.write(chunk(b"IDAT",comp)); f.write(chunk(b"IEND",b""))

def main():
    a = draw("closed","smile"); b = draw("closed","open")
    c = draw("open","smile")
    if "--png" in sys.argv:
        root=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        d=os.path.join(root,"build","faces_preview"); os.makedirs(d,exist_ok=True)
        # 三张并排
        import zlib, struct
        imgs=[]
        for g in (a,b,c):
            to_png(g, os.path.join(d,"_xiaoai_tmp.png"))
            from PIL import Image
            imgs.append(Image.open(os.path.join(d,"_xiaoai_tmp.png")).convert("L"))
        pad=10; Wt=sum(i.width for i in imgs)+pad*(len(imgs)+1); Ht=max(i.height for i in imgs)+2*pad
        out=Image.new("L",(Wt,Ht),255); x=pad
        for i in imgs: out.paste(i,(x,pad)); x+=i.width+pad
        out.save(os.path.join(d,"_xiaoai.png")); print("saved", os.path.join(d,"_xiaoai.png"))
    else:
        ascii(a,"closed"); ascii(b,"closed+open-mouth"); ascii(c,"open")

if __name__=="__main__":
    main()
