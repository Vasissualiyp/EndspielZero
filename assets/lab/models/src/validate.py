# SPDX-License-Identifier: GPL-3.0-or-later
"""Strict OBJ validation, including welded topology and component winding.

Hard normal edges deliberately duplicate OBJ vertices. Weld coordinates within each
named manufacturing component before counting edges; do not merge touching parts.
No dependencies. Run with --self-test to exercise deliberately broken meshes.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
from mesh import cross, dot, sub

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parents[2] / 'out' / 'm24-models'
# Assembly-space AABBs. Split materials retain the body's origin, not their own base.
SPECS = {
    'projector_body': ((-.12,-.12,0),(.12,.12,.074),'Base centre; front −Y.'),
    'projector_lens': ((-.084,-.084,.064),(.084,.084,.075),'Projector base centre; disc top centre exactly (0, 0, 0.075).'),
    'projector_leds': ((-.096,-.1065,.042),(.096,.1065,.046),'Projector base centre; six collar indicators.'),
    'lamp': ((-.09,-.35,0),(.09,.08,.55),'Weighted base centre; reaches forward toward −Y.'),
    'lamp_head_emitter': (None,None,'Lamp base centre; tilted 2 mm emitter plate.'),
    'magazine': ((-.25,-.5,0),(0,.5,1.1),'Back plane X=0, width centre Y=0, bottom Z=0; front −X.'),
    'magazine_leds': ((-.252,-.394,.0505),(-.249,.394,.0595),'Same wall/back-plane origin as magazine; front −X.'),
    'gantry_carriage': ((-.15,-.15,0),(.15,.15,.09),'Bottom-face centre; mast socket on Z axis.'),
    'gantry_mast_outer': ((-.025,-.025,-.6),(.025,.025,0),'Top centre; extends downward (−Z). Tube Ø0.046, collar Ø0.050.'),
    'gantry_mast_inner': ((-.045,-.045,-.6),(.045,.045,0),'Top centre; extends downward (−Z). Tube Ø0.038, four-prong clamp Ø0.090.'),
    'slab_back': ((-.22,-.22,-.025),(.22,.22,0),'Centre of open LED plane at Z=0; hardware below that plane.'),
    'bench_monitor': ((-.25,-.09,0),(.25,.09,.36),'Stand/base centre; screen faces −Y.'),
    'clutter_mug': (None,None,'Cup base centre; handle toward +X.'),
    'clutter_toolbox': (None,None,'Case footprint centre at bottom Z=0; latches face −Y.'),
    'clutter_spool': (None,None,'Bottom-flange centre; spool axis +Z.'),
    'clutter_multimeter': (None,None,'Meter case footprint centre at bottom Z=0; controls face +Z, jacks toward −Y.'),
    'door_roller': ((-1.5,-.04,0),(1.5,.04,2.4),'Bottom centre; ribbed room-facing side is +Y.'),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_obj(path):
    vertices, normals, faces = [], [], []
    group = 'default'
    for lineno, line in enumerate(path.read_text(encoding='ascii').splitlines(), 1):
        row = line.split()
        if not row or row[0].startswith('#'):
            continue
        tag = row[0]
        require(tag in ('v','vn','f','o','g','s'), f'line {lineno}: forbidden OBJ directive {tag}')
        if tag in ('v','vn'):
            require(len(row) == 4, f'line {lineno}: expected three coordinates')
            v = tuple(map(float,row[1:]))
            require(all(math.isfinite(x) for x in v), f'line {lineno}: non-finite vector')
            (vertices if tag == 'v' else normals).append(v)
        elif tag == 'g':
            require(len(row) == 2, f'line {lineno}: expected one group name')
            group = row[1]
        elif tag == 'f':
            require(len(row) in (4,5), f'line {lineno}: face must be triangle or quad')
            ids=[]
            for corner in row[1:]:
                pair=corner.split('//')
                require(len(pair)==2, f'line {lineno}: each corner needs v//vn')
                v,n=map(int,pair)
                require(0 < v <= len(vertices) and 0 < n <= len(normals),f'line {lineno}: invalid index')
                ids.append((v-1,n-1))
            for i in range(1,len(ids)-1):
                faces.append((group,(ids[0],ids[i],ids[i+1])))
    return vertices,normals,faces


def geometry(vertices,normals,faces):
    require(vertices and normals and faces,'empty mesh')
    require(len(vertices)<=65535,'vertex budget exceeded')
    require(len(faces)<=6000,'triangle budget exceeded')
    for n in normals:
        require(abs(dot(n,n)-1)<2e-6,'non-unit normal')
    edges=defaultdict(list)
    group_faces=defaultdict(list)
    seen=set()
    used=set()
    for index,(group,face) in enumerate(faces):
        p=[vertices[v] for v,n in face]
        used.update(v for v,n in face)
        normal=cross(sub(p[1],p[0]),sub(p[2],p[0]))
        require(dot(normal,normal)>1e-24,'degenerate face')
        key=tuple(sorted(p))
        require(key not in seen,'duplicate geometric face')
        seen.add(key)
        for v,n in face:
            require(dot(normal,normals[n])>0,'normal opposes face winding')
        group_faces[group].append(index)
        for a,b in zip(p,p[1:]+p[:1]):
            edges[(group,tuple(sorted((a,b))))].append((a,b,index))
    require(len(used)==len(vertices),'unused vertices')
    neighbors=defaultdict(set)
    for edge,uses in edges.items():
        require(len(uses)==2,f'non-watertight edge in {edge[0]}: {len(uses)} faces')
        a,b,i=uses[0]; c,d,j=uses[1]
        require(a==d and b==c, f'inconsistent edge winding in {edge[0]}')
        neighbors[i].add(j); neighbors[j].add(i)
    # Check every connected shell, not just total volume (which can mask an inward part).
    remaining=set(range(len(faces)))
    volumes=[]
    while remaining:
        first=remaining.pop(); pending=[first]; component=[first]
        while pending:
            for j in neighbors[pending.pop()]:
                if j in remaining:
                    remaining.remove(j); pending.append(j); component.append(j)
        origin=vertices[faces[first][1][0][0]]
        volume=0
        for i in component:
            p=[sub(vertices[v],origin) for v,n in faces[i][1]]
            volume+=dot(p[0],cross(p[1],p[2]))/6
        require(volume>1e-14, 'non-positive signed volume in closed shell')
        volumes.append(volume)
    low=tuple(min(p[i] for p in vertices) for i in range(3))
    high=tuple(max(p[i] for p in vertices) for i in range(3))
    return {'triangles':len(faces),'vertices':len(vertices),'bounds_min':low,'bounds_max':high,
            'size':tuple(b-a for a,b in zip(low,high)), 'closed_shells':len(volumes),
            'signed_volume':sum(volumes),'watertight':True}


def ray_hits(vertices, faces, origin, direction, group=None):
    """Positive ray intersections, deduplicated across triangle diagonals."""
    hits=[]
    for label,face in faces:
        if group is not None and label != group: continue
        a,b,c=[vertices[j] for j,k in face]
        edge1=sub(b,a); edge2=sub(c,a); h=cross(direction,edge2)
        determinant=dot(edge1,h)
        if abs(determinant)<1e-13: continue
        inv=1/determinant; offset=sub(origin,a)
        u=inv*dot(offset,h)
        if u < -1e-8 or u > 1+1e-8: continue
        q=cross(offset,edge1); v=inv*dot(direction,q)
        if v < -1e-8 or u+v > 1+1e-8: continue
        t=inv*dot(edge2,q)
        if t>1e-9 and not any(abs(t-other)<1e-8 for other in hits): hits.append(t)
    return sorted(hits)


def validate(path):
    v,n,f=read_obj(path)
    result=geometry(v,n,f)
    expected_low,expected_high,origin=SPECS[path.stem]
    if expected_low:
        for i in range(3):
            span=expected_high[i]-expected_low[i]
            require(abs(result['size'][i]-span)<=span*.05+1e-8,f'axis {i}: size outside ±5%')
            for actual,expected in ((result['bounds_min'][i],expected_low[i]),(result['bounds_max'][i],expected_high[i])):
                require(abs(actual-expected)<=span*.05+1e-8,f'axis {i}: wrong origin/bounds')
    if path.stem.startswith('clutter_'):
        require(max(result['size'])<=.25,'clutter exceeds 0.25 m')
        require(abs(result['bounds_min'][2])<1e-8,'clutter base not at Z=0')
    if path.stem=='gantry_mast_inner':
        hits=ray_hits(v,f,(.024,.001,-.569),(1,0,0),'clamp_ring')
        require(len(hits)%2==1,'clamp ring floats: missing radial bridge to the inner tube')
    if path.stem=='door_roller':
        for i in range(1,24):
            hits=ray_hits(v,f,(-.873,-.10,i*.1-.003),(0,1,0))
            require(hits,f'door has a through-gap at slat seam {i}')
    groups=Counter(g for g,face in f)
    if path.stem=='projector_lens':
        require((0.,0.,.075) in v and result['bounds_max'][2]==.075,'wrong hologram anchor')
        require(sum(g.startswith('lens_') for g in groups)==7,'expected seven lenses')
    if path.stem=='projector_leds': require(len(groups)==6,'expected six LED pills')
    if path.stem=='magazine_leds': require(len(groups)==8,'expected eight slot indicators')
    if path.stem=='magazine':
        for i in range(8):
            a=[v[j][1] for g,face in f if g==f'bay_{i}_guide_-1' for j,k in face]
            b=[v[j][1] for g,face in f if g==f'bay_{i}_guide_1' for j,k in face]
            require(abs(min(b)-max(a)-.030)<1e-8,'slot width is not 30 mm')
            require(abs((min(b)+max(a))/2-(i-3.5)*.11)<1e-8,'slot pitch is not 110 mm')
    if path.stem=='lamp_head_emitter':
        # Undo head rotation: its actual normal thickness is 2 mm, not its world Z AABB.
        a=math.radians(20)
        z=[-math.sin(a)*(p[1]+.320)+math.cos(a)*(p[2]-.528) for p in v]
        require(abs(max(z)-min(z)-.002)<2e-8,'emitter thickness is not 2 mm')
    if path.stem=='slab_back':
        for g,face in f:
            p=[v[j] for j,k in face]
            if all(abs(q[2])<1e-9 for q in p):
                centre=tuple(sum(q[i] for q in p)/3 for i in range(2))
                require(max(map(abs,centre))>.20,'LED aperture is covered at Z=0')
    result['origin']=origin
    result['file']=path.name
    return result


def self_test():
    # Real fixture parsed from generated assets; mutations must fail independently.
    v,n,f=read_obj(ROOT/'gantry_mast_outer.obj')
    geometry(v,n,f)
    cases={
        'missing face':(v,n,f[:-1]),
        'duplicate face':(v,n,f+[f[0]]),
        'degenerate face':(v,n,[(f[0][0],(f[0][1][0],)*3)]+f[1:]),
        'non-unit normal':(v,[(2,0,0)]+n[1:],f),
        'reversed face':(v,n,[(f[0][0],tuple(reversed(f[0][1])))]+f[1:]),
        'inward shell':(v,[tuple(-x for x in a) for a in n],[(g,tuple(reversed(face))) for g,face in f]),
        'over budget':(v,n,f*10),
    }
    for name,args in cases.items():
        try: geometry(*args)
        except ValueError: continue
        raise AssertionError('validator accepted '+name)
    print(f'Validator self-test: {len(cases)} invalid meshes correctly rejected.')


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--self-test',action='store_true'); args=parser.parse_args()
    results=[]; failures=[]
    actual={p.stem for p in ROOT.glob('*.obj')}
    require(actual==set(SPECS),f'missing/extra files: {actual ^ set(SPECS)}')
    for name in SPECS:
        try:
            r=validate(ROOT/(name+'.obj')); results.append(r)
            print(f"PASS {r['file']}: {r['triangles']} triangles, {r['vertices']} vertices; {r['bounds_min']} .. {r['bounds_max']}; {r['closed_shells']} closed shells, outward")
        except ValueError as e:
            failures.append(name); print(f'FAIL {name}.obj: {e}')
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
    if args.self_test: self_test()
    require(not failures,'validation failed: '+', '.join(failures))

if __name__=='__main__': main()
