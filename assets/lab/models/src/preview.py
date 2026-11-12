# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic, dependency-free PNG rasterizer with depth and smooth lighting.

Two orthographic review views per model plus labelled contact sheets. No graphics
server, game changes, third-party code, external assets or installed packages.
"""
import argparse
import math
from pathlib import Path
import struct
import zlib
from mesh import add, sub, mul, dot, cross, unit
from validate import read_obj, ROOT, OUT, SPECS

W,H=560,440
FONT={
 'A':['01110','10001','10001','11111','10001','10001','10001'],
 'B':['11110','10001','10001','11110','10001','10001','11110'],
 'C':['01111','10000','10000','10000','10000','10000','01111'],
 'D':['11110','10001','10001','10001','10001','10001','11110'],
 'E':['11111','10000','10000','11110','10000','10000','11111'],
 'F':['11111','10000','10000','11110','10000','10000','10000'],
 'G':['01111','10000','10000','10111','10001','10001','01111'],
 'H':['10001','10001','10001','11111','10001','10001','10001'],
 'I':['11111','00100','00100','00100','00100','00100','11111'],
 'J':['00111','00010','00010','00010','10010','10010','01100'],
 'K':['10001','10010','10100','11000','10100','10010','10001'],
 'L':['10000','10000','10000','10000','10000','10000','11111'],
 'M':['10001','11011','10101','10101','10001','10001','10001'],
 'N':['10001','11001','10101','10011','10001','10001','10001'],
 'O':['01110','10001','10001','10001','10001','10001','01110'],
 'P':['11110','10001','10001','11110','10000','10000','10000'],
 'Q':['01110','10001','10001','10001','10101','10010','01101'],
 'R':['11110','10001','10001','11110','10100','10010','10001'],
 'S':['01111','10000','10000','01110','00001','00001','11110'],
 'T':['11111','00100','00100','00100','00100','00100','00100'],
 'U':['10001','10001','10001','10001','10001','10001','01110'],
 'V':['10001','10001','10001','10001','10001','01010','00100'],
 'W':['10001','10001','10001','10101','10101','10101','01010'],
 'X':['10001','10001','01010','00100','01010','10001','10001'],
 'Y':['10001','10001','01010','00100','00100','00100','00100'],
 'Z':['11111','00001','00010','00100','01000','10000','11111'],
 '0':['01110','10001','10011','10101','11001','10001','01110'],
 '1':['00100','01100','00100','00100','00100','00100','01110'],
 '2':['01110','10001','00001','00010','00100','01000','11111'],
 '3':['11110','00001','00001','01110','00001','00001','11110'],
 '4':['00010','00110','01010','10010','11111','00010','00010'],
 '5':['11111','10000','10000','11110','00001','00001','11110'],
 '6':['01110','10000','10000','11110','10001','10001','01110'],
 '7':['11111','00001','00010','00100','01000','01000','01000'],
 '8':['01110','10001','10001','01110','10001','10001','01110'],
 '9':['01110','10001','10001','01111','00001','00001','01110'],
 '_':['00000']*6+['11111'], '-':['00000']*3+['11111']+['00000']*3,
 '.':['00000']*6+['00100'], '/':['00001','00001','00010','00100','01000','10000','10000'],
}

def label(buf,w,h,x,y,text,color=(190,201,212),scale=1):
    for ch in text.upper():
        for row,bits in enumerate(FONT.get(ch,['00000']*7)):
            for col,bit in enumerate(bits):
                if bit=='1':
                    for dy in range(scale):
                        for dx in range(scale):
                            xx=x+col*scale+dx; yy=y+row*scale+dy
                            if 0<=xx<w and 0<=yy<h:
                                i=(yy*w+xx)*3; buf[i:i+3]=bytes(color)
        x+=6*scale


def png(path,w,h,buf):
    def chunk(tag,data): return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    raw=b''.join(b'\0'+buf[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b''))


def render(name,view,overrides=None,only_groups=None):
    vertices,normals,faces=read_obj(ROOT/(name+'.obj'))
    if only_groups:
        faces=[(g,f) for g,f in faces if g in only_groups]
        indices=sorted({i for g,f in faces for i,n in f})
        remap={old:new for new,old in enumerate(indices)}
        vertices=[vertices[i] for i in indices]
        faces=[(g,tuple((remap[i],n) for i,n in f)) for g,f in faces]
    eye=(1,-1.7,1.05) if view=='front' else (1,0,.28)
    if name.startswith('magazine'): eye=(-1.7,-1,1) if view=='front' else (-.18,-1,.25)
    if name=='door_roller': eye=(1,2,.8) if view=='front' else (1,.25,.2)
    if name=='slab_back': eye=(1,-1.6,-1.6) if view=='front' else (1,0,.55)
    if name=='gantry_carriage': eye=(1,-1.7,-.85) if view=='front' else (1,0,.7)
    if name in ('projector_lens','lamp_head_emitter'): eye=(1,-1.7,-1.2) if view=='front' else (1,0,.4)
    if overrides: eye=overrides
    forward=unit(eye); right=unit(cross((0,0,1),forward)); up=cross(forward,right)
    projected=[(dot(p,right),dot(p,up),dot(p,forward)) for p in vertices]
    lo=[min(p[i] for p in projected) for i in range(2)]; hi=[max(p[i] for p in projected) for i in range(2)]
    scale=min((W-70)/(hi[0]-lo[0]),(H-100)/(hi[1]-lo[1]))
    cx=(hi[0]+lo[0])/2; cy=(hi[1]+lo[1])/2
    pts=[((p[0]-cx)*scale+W/2,H/2+8-(p[1]-cy)*scale,p[2]) for p in projected]
    buf=bytearray(W*H*3)
    for y in range(H):
        shade=int(24+8*y/H)
        buf[y*W*3:(y+1)*W*3]=bytes((shade,shade+3,shade+8))*W
    depth=[-math.inf]*(W*H)
    # Soft key, fill and rim in view space; bright enough to inspect dark props.
    key=unit(add(mul(forward,1),add(mul(right,-.7),mul(up,1.3))))
    fill=unit(add(mul(forward,.5),mul(right,1)))
    half=unit(add(key,forward))
    base=(124,137,147)
    if 'leds' in name or 'emitter' in name: base=(115,196,200)
    if name=='projector_lens': base=(151,177,184)
    for group,face in faces:
        pa,pb,pc=[pts[i] for i,n in face]
        va,vb,vc=[vertices[i] for i,n in face]
        if dot(cross(sub(vb,va),sub(vc,va)),forward)<=0: continue
        ax,ay,az=pa; bx,by,bz=pb; cx_,cy_,cz=pc
        den=(by-cy_)*(ax-cx_)+(cx_-bx)*(ay-cy_)
        if abs(den)<1e-10: continue
        xmin=max(0,math.floor(min(ax,bx,cx_))); xmax=min(W-1,math.ceil(max(ax,bx,cx_)))
        ymin=max(0,math.floor(min(ay,by,cy_))); ymax=min(H-1,math.ceil(max(ay,by,cy_)))
        na,nb,nc=[normals[n] for i,n in face]
        for y in range(ymin,ymax+1):
            py=y+.5
            for x in range(xmin,xmax+1):
                px=x+.5
                a=((by-cy_)*(px-cx_)+(cx_-bx)*(py-cy_))/den
                b=((cy_-ay)*(px-cx_)+(ax-cx_)*(py-cy_))/den
                c=1-a-b
                if min(a,b,c)<-1e-8: continue
                z=a*az+b*bz+c*cz; index=y*W+x
                if z<=depth[index]: continue
                depth[index]=z
                nx=a*na[0]+b*nb[0]+c*nc[0]; ny=a*na[1]+b*nb[1]+c*nc[1]; nz=a*na[2]+b*nb[2]+c*nc[2]
                norm=1/math.sqrt(nx*nx+ny*ny+nz*nz); nx*=norm; ny*=norm; nz*=norm
                kd=max(0,nx*key[0]+ny*key[1]+nz*key[2]); fd=max(0,nx*fill[0]+ny*fill[1]+nz*fill[2])
                spec=max(0,nx*half[0]+ny*half[1]+nz*half[2])**40
                shade=.23+.80*kd+.23*fd
                color=tuple(min(255,int(v*shade+42*spec)) for v in base)
                buf[index*3:index*3+3]=bytes(color)
    label(buf,W,H,14,12,name,scale=2)
    label(buf,W,H,14,34,'3/4 FRONT' if view=='front' else 'SIDE',color=(116,146,170))
    if name in ('slab_back','gantry_carriage','projector_lens','lamp_head_emitter') and view=='front':
        label(buf,W,H,120,34,'UNDERSIDE INSPECTION',color=(116,146,170))
    label(buf,W,H,14,H-20,f'{len(faces)} TRIANGLES / METRES / Z UP',color=(116,146,170))
    return buf


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--models',nargs='*',choices=list(SPECS),help='Re-render contact sheets containing these models only')
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    names=list(SPECS)
    for start in range(0,len(names),3):
        batch=names[start:start+3]
        if args.models and not set(batch).intersection(args.models): continue
        sheet=bytearray(W*2*H*len(batch)*3)
        for row,name in enumerate(batch):
            for column,view in enumerate(('front','side')):
                buf=render(name,view)
                png(OUT/f'{name}-{view}.png',W,H,buf)
                for y in range(H):
                    dest=((row*H+y)*W*2+column*W)*3
                    sheet[dest:dest+W*3]=buf[y*W*3:(y+1)*W*3]
            print('Rendered '+name,flush=True)
        png(OUT/f'review-{start//3+1:02}.png',W*2,H*len(batch),sheet)

    clamp_groups={'clamp_ring'} | {f'grip_{part}_{i}' for part in ('prong','lip') for i in range(4)}
    for view,eye in [('top',(1,-1.7,1.2)),('bottom',(1,-1.7,-1.2))]:
        buf=render('gantry_mast_inner','front',eye,clamp_groups)
        png(OUT/f'gantry_mast_inner-clamp-{view}.png',W,H,buf)

if __name__=='__main__': main()
