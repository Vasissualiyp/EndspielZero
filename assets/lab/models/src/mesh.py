# SPDX-License-Identifier: GPL-3.0-or-later
"""Dependency-free metre/Z-up mesh primitives; components are closed shells."""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def add(a, b): return tuple(x+y for x,y in zip(a,b))
def sub(a, b): return tuple(x-y for x,y in zip(a,b))
def mul(a, s): return tuple(x*s for x in a)
def dot(a, b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a): return mul(a, 1/math.sqrt(dot(a,a)))

def frame(a,b):
    z=unit(sub(b,a)); x=unit(cross((0,1,0) if abs(z[1])<.9 else (1,0,0),z)); y=cross(z,x)
    return lambda p: add(a,add(mul(x,p[0]),add(mul(y,p[1]),mul(z,p[2]))))

def tilt(center, angle):
    c,s=math.cos(angle),math.sin(angle)
    return lambda p: add(center,(p[0],c*p[1]-s*p[2],s*p[1]+c*p[2]))

class Mesh:
    def __init__(self): self.parts=[]
    def shell(self, name, polygons, transform=lambda p:p):
        v=[]; f=[]; lookup={}
        for poly in polygons:
            ids=[]
            for p in poly:
                p=tuple(round(x,10) for x in transform(p))
                if p not in lookup: lookup[p]=len(v); v.append(p)
                ids.append(lookup[p])
            for i in range(1,len(ids)-1): f.append((ids[0],ids[i],ids[i+1]))
        volume=sum(dot(v[a],cross(v[b],v[c])) for a,b,c in f)/6
        if volume<0: f=[(a,c,b) for a,b,c in f]
        self.parts.append((name,v,f))
    def box(self,name,center,size,bevel=.003,transform=lambda p:p):
        h=[s/2 for s in size]; d=min(bevel,min(h)*.45); polys=[]
        # Six inset face planes, twelve bevel planes, eight corner planes.
        for axis in range(3):
            other=[i for i in range(3) if i!=axis]
            for sign in (-1,1):
                poly=[]
                for u,w in ((-1,-1),(1,-1),(1,1),(-1,1)):
                    p=[0,0,0]; p[axis]=sign*h[axis]
                    p[other[0]]=u*(h[other[0]]-d); p[other[1]]=w*(h[other[1]]-d); poly.append(p)
                polys.append(poly)
        for a,b in ((0,1),(0,2),(1,2)):
            k=3-a-b
            for sa in (-1,1):
                for sb in (-1,1):
                    poly=[]
                    for inset_a,sk in ((False,-1),(False,1),(True,1),(True,-1)):
                        p=[0,0,0]; p[a]=sa*(h[a]-(d if inset_a else 0)); p[b]=sb*(h[b]-(0 if inset_a else d)); p[k]=sk*(h[k]-d); poly.append(p)
                    polys.append(poly)
        for sx in (-1,1):
            for sy in (-1,1):
                for sz in (-1,1):
                    polys.append([[s*(h[i]-(0 if i==a else d)) for i,s in enumerate((sx,sy,sz))] for a in range(3)])
        for poly in polys:
            n=cross(sub(poly[1],poly[0]),sub(poly[2],poly[0]))
            if dot(n,poly[0])<0: poly.reverse()
        self.shell(name,polys,lambda p:transform(add(center,p)))
    def lathe(self,name,profile,n=48,transform=lambda p:p):
        # Closed profile in radius/Z, including axis points for solid end caps.
        rings=[[(r*math.cos(2*math.pi*i/n),r*math.sin(2*math.pi*i/n),z) for i in range(n)] for r,z in profile]
        polys=[]
        for j in range(len(rings)):
            k=(j+1)%len(rings)
            if profile[j][0]==profile[k][0]==0: continue
            for i in range(n):
                q=(i+1)%n
                if profile[j][0]==0: poly=[rings[j][i],rings[k][q],rings[k][i]]
                elif profile[k][0]==0: poly=[rings[j][i],rings[j][q],rings[k][i]]
                else: poly=[rings[j][i],rings[j][q],rings[k][q],rings[k][i]]
                polys.append(poly)
        self.shell(name,polys,transform)
    def cylinder(self,name,a,b,r,n=32,bevel=.002):
        length=math.sqrt(dot(sub(b,a),sub(b,a))); d=min(bevel,length*.24,r*.3)
        self.lathe(name,[(0,0),(r-d,0),(r,d),(r,length-d),(r-d,length),(0,length)],n,frame(a,b))
    def tube(self,name,r,inner,z0,z1,n=48,bevel=.002,transform=lambda p:p):
        d=min(bevel,(r-inner)*.35,(z1-z0)*.24)
        self.lathe(name,[(inner+d,z0),(r-d,z0),(r,z0+d),(r,z1-d),(r-d,z1),(inner+d,z1),(inner,z1-d),(inner,z0+d)],n,transform)
    def beam(self,name,a,b,width,depth):
        length=math.sqrt(dot(sub(b,a),sub(b,a)))
        self.box(name,(0,0,length/2),(width,depth,length),transform=frame(a,b))
    def wire(self,name,points,r=.002,n=8):
        # Swept closed wire; useful for the spring, handle and cable loops.
        rings=[]
        for i,p in enumerate(points):
            tangent=sub(points[min(i+1,len(points)-1)],points[max(0,i-1)])
            tr=frame(p,add(p,tangent))
            rings.append([tr((r*math.cos(2*math.pi*j/n),r*math.sin(2*math.pi*j/n),0)) for j in range(n)])
        polys=[list(reversed(rings[0])),rings[-1]]
        for a,b in zip(rings,rings[1:]):
            for j in range(n): polys.append([a[j],a[(j+1)%n],b[(j+1)%n],b[j]])
        self.shell(name,polys)
    def write(self,name):
        lines=['# SPDX-License-Identifier: GPL-3.0-or-later','# source: Original procedural geometry for EndspielZero; no downloaded assets.', '# Generated by src/generate.py; metres, Z up; no materials.', 'o '+name]
        count=0
        for label,v,f in self.parts:
            normals=[unit(cross(sub(v[b],v[a]),sub(v[c],v[a]))) for a,b,c in f]
            adjacent=[[] for p in v]
            for i,face in enumerate(f):
                for j in face: adjacent[j].append(i)
            local={}; outf=[]; values=[]
            for i,face in enumerate(f):
                out=[]
                for j in face:
                    # Only adjacent faces within 30 degrees contribute; hard edges split.
                    ns=[normals[k] for k in adjacent[j] if dot(normals[k],normals[i])>math.cos(math.pi/6)-1e-7]
                    nn=unit(tuple(sum(n[a] for n in ns) for a in range(3)))
                    key=(j,tuple(round(x,7) for x in nn))
                    if key not in local: local[key]=count+len(values)+1; values.append((v[j],nn))
                    out.append(local[key])
                outf.append(out)
            lines.append('g '+label)
            for p,n in values: lines.append('v '+' '.join(f'{x:.9f}' for x in p))
            for p,n in values: lines.append('vn '+' '.join(f'{x:.9f}' for x in n))
            for face in outf: lines.append('f '+' '.join(f'{i}//{i}' for i in face))
            count+=len(values)
        (ROOT/(name+'.obj')).write_text('\n'.join(lines)+'\n',encoding='ascii')
