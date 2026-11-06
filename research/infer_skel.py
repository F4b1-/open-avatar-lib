"""Infer bone adjacency and joint positions from skin weights of T-posed meshes."""
import sys,os,struct,collections,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from model import expand
K=(2.0,2.0/math.sqrt(1.5),math.sqrt(3.0))
SIG=bytes.fromhex('0244ed36')
def verts(d):
    n=int.from_bytes(d,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    hdr=struct.unpack_from('<6I',d,0); q=d.find(SIG)-4; out=[]
    while q>=0 and hdr[5]:
        nv=struct.unpack_from('<I',d,q)[0]; step,*mn=struct.unpack_from('<4f',d,q+4); s=(q+20)*8
        w=[val(s+6*k,6) for k in range(3)]; el=[val(s+18+32*k,32) for k in range(8)]
        vstart=s+274; fixed=sum(w)+el[1]+el[3]+el[5]+el[7]; iw=max(1,(nv-1).bit_length())
        for th in range(0,13):
            ib=(vstart+(fixed+16*th)*nv+7)//8; c,z,ww=struct.unpack_from('<IHH',d,ib)
            if z==0 and ww==iw and c%3==0 and c>0: break
        rec=fixed+16*th
        for i in range(nv):
            b=vstart+rec*i; p=[]; o=0
            for a in range(3): p.append(mn[a]+val(b+o,w[a])*step*K[a]); o+=w[a]
            o+=el[1]; ws=(val(b+o,el[3])+el[2])&0xffffffff; o+=el[3]; bi=(val(b+o,el[5])+el[4])&0xffffffff
            out.append((tuple(p),[(bi>>(8*k))&255 for k in range(4)],[((ws>>(8*k))&255)/255 for k in range(4)]))
        p2=d.find(SIG,((ib+8)*8+c*iw+7)//8); q=p2-4 if p2>=0 else -1
        if len(out)>=sum(1 for _ in [0]) and q<0: break
    return out
V=[]
for f in sys.argv[1:]:
    d=open(f,'rb').read()
    for bid,at,ln,fs in blocks(d):
        if bid==3: V+=verts(expand(d[at:at+ln]))
edge=collections.Counter(); jsum=collections.defaultdict(lambda:[0,0,0,0]); bsum=collections.defaultdict(lambda:[0,0,0,0])
for p,b,w in V:
    for i in range(4):
        if w[i]>0:
            s=bsum[b[i]]; s[0]+=p[0]*w[i]; s[1]+=p[1]*w[i]; s[2]+=p[2]*w[i]; s[3]+=w[i]
        for j in range(i+1,4):
            if w[i]>0 and w[j]>0 and b[i]!=b[j]:
                e=tuple(sorted((b[i],b[j]))); x=min(w[i],w[j]); edge[e]+=x
                s=jsum[e]; s[0]+=p[0]*x; s[1]+=p[1]*x; s[2]+=p[2]*x; s[3]+=x
bones=sorted(bsum)
# maximum spanning tree over co-weight
parent={}; inT={0}; 
import heapq
while True:
    best=None
    for (a,b),wt in edge.items():
        if (a in inT)!=(b in inT) and (best is None or wt>best[0]): best=(wt,a,b)
    if not best: break
    wt,a,b=best; child,par=(b,a) if a in inT else (a,b); parent[child]=par; inT.add(child)
print('bones with weight',len(bones),'tree size',len(inT),'unreached',[b for b in bones if b not in inT])
def c(s): return tuple(round(s[k]/s[3],3) for k in range(3))
for b in sorted(parent):
    e=tuple(sorted((b,parent[b])))
    print(f"bone {b:2d} parent {parent[b]:2d}  joint {c(jsum[e])}  bone centroid {c(bsum[b])}  coweight {edge[e]:.1f}")
