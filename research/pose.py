"""Pose a prop that carries its own skeleton (block 5) through an animation's prop part and draw side views
(SPECS.md 4.4, 4.5). Checks the skeleton and animation conventions on the sheep and teddy bear.

  python3 -I pose.py <prop .bin> <animation .bin> <out.png> [frame,frame,...]

Skinning: v' = sum_i w_i * World_i(t) * World_i(rest)^-1 * v. Local transform of a bone at frame t: its rest local
transform (parent rest frame^-1 * own rest frame) followed by translate(T) rotate(R) scale(S), the animation's
values in the bone's own frame.
"""
import sys,os,struct,math,zlib
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from model import expand
import skeleton,anim
K=skeleton.K
SIG=bytes.fromhex('0244ed36')
def mm(A,B): return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def mv(A,v): return [sum(A[i][k]*v[k] for k in range(3)) for i in range(3)]
def tr(A): return [list(r) for r in zip(*A)]
def inv(X): Rt=tr(X[0]); return Rt,[-x for x in mv(Rt,X[1])]
def comp(X,Y): return mm(X[0],Y[0]),[a+b for a,b in zip(mv(X[0],Y[1]),X[1])]
def skinned_meshes(d):
    """Meshes of an expanded model block: vertices (position, 4 bone indices, 4 weights) and triangles."""
    n=int.from_bytes(d,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    hdr=struct.unpack_from('<6I',d,0); q=d.find(SIG)-4; out=[]
    while q>=0 and len(out)<hdr[5]:
        nv=struct.unpack_from('<I',d,q)[0]; step,*mn=struct.unpack_from('<4f',d,q+4); s=(q+20)*8
        w=[val(s+6*k,6) for k in range(3)]; el=[val(s+18+32*k,32) for k in range(8)]
        vstart=s+274; fixed=sum(w)+el[1]+el[3]+el[5]+el[7]; iw=max(1,(nv-1).bit_length())
        for th in range(0,13):
            ib=(vstart+(fixed+16*th)*nv+7)//8; c,z,ww=struct.unpack_from('<IHH',d,ib)
            if z==0 and ww==iw and c%3==0 and c>0: break
        rec=fixed+16*th; V=[]
        for i in range(nv):
            b=vstart+rec*i; p=[]; o=0
            for a in range(3): p.append(mn[a]+val(b+o,w[a])*step*K[a]); o+=w[a]
            o+=el[1]; ws=(val(b+o,el[3])+el[2])&0xffffffff; o+=el[3]; bi=(val(b+o,el[5])+el[4])&0xffffffff
            V.append((p,[(bi>>(8*k))&255 for k in range(4)],[((ws>>(8*k))&255)/255 for k in range(4)]))
        idx=[val((ib+8)*8+iw*k,iw) for k in range(c)]
        out.append(dict(v=V,tris=[idx[k:k+3] for k in range(0,c,3)]))
        p2=d.find(SIG,((ib+8)*8+c*iw+7)//8); q=p2-4 if p2>=0 else -1
    return out
def pose(rest,vals):
    """World transform (R, t) of each bone for one frame's (T, R, S) values."""
    W=[]
    for i,(p,R0,t0) in enumerate(rest):
        Tt,Rv,Sv=vals[i]
        M=(mm(skeleton.rotvec([2*x for x in Rv]),[[Sv[0],0,0],[0,Sv[1],0],[0,0,Sv[2]]]),list(Tt))
        L=(R0,t0) if p==255 else comp(inv((rest[p][1],rest[p][2])),(R0,t0))
        M=comp(L,M); W.append(M if p==255 else comp(W[p],M))
    return W
def skin(mesh,rest,W):
    S=[comp(W[i],inv((R,t))) for i,(p,R,t) in enumerate(rest)]; out=[]
    for p,b,w in mesh['v']:
        v=[0.0,0.0,0.0]
        for k in range(4):
            if w[k]>0:
                R,t=S[b[k]]; q=mv(R,p)
                for a in range(3): v[a]+=w[k]*(q[a]+t[a])
        out.append(v)
    return out
def draw(polys,tris,out,cell=200,cols=5,ax=(2,1,0)):
    """Flat-shaded orthographic views, one cell per pose, all on one scale (ax: horizontal, vertical, depth)."""
    rows=(len(polys)+cols-1)//cols; W=cell*cols; H=cell*rows
    img=bytearray([40,40,48]*W*H); zb=[-1e9]*(W*H)
    allp=[v for P in polys for v in P]
    lo=[min(v[a] for v in allp) for a in range(3)]; hi=[max(v[a] for v in allp) for a in range(3)]
    sc=(cell*0.9)/max(hi[ax[0]]-lo[ax[0]],hi[ax[1]]-lo[ax[1]],1e-9)
    for i,P in enumerate(polys):
        ox=(i%cols)*cell; oy=(i//cols)*cell
        for t in tris:
            a,b,c=(P[j] for j in t)
            u=[b[j]-a[j] for j in range(3)]; v=[c[j]-a[j] for j in range(3)]
            nrm=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]); L=math.sqrt(sum(x*x for x in nrm)) or 1
            shade=0.3+0.7*abs(nrm[ax[2]])/L
            pts=[(ox+5+(p[ax[0]]-lo[ax[0]])*sc,oy+cell-5-(p[ax[1]]-lo[ax[1]])*sc,p[ax[2]]) for p in (a,b,c)]
            x0=max(int(min(p[0] for p in pts)),ox); x1=min(int(max(p[0] for p in pts))+1,ox+cell-1)
            y0=max(int(min(p[1] for p in pts)),oy); y1=min(int(max(p[1] for p in pts))+1,oy+cell-1)
            (X0,Y0,Z0),(X1,Y1,Z1),(X2,Y2,Z2)=pts; den=(Y1-Y2)*(X0-X2)+(X2-X1)*(Y0-Y2)
            if abs(den)<1e-12: continue
            for y in range(y0,y1+1):
                for x in range(x0,x1+1):
                    l0=((Y1-Y2)*(x-X2)+(X2-X1)*(y-Y2))/den; l1=((Y2-Y0)*(x-X2)+(X0-X2)*(y-Y2))/den; l2=1-l0-l1
                    if l0<0 or l1<0 or l2<0: continue
                    z=l0*Z0+l1*Z1+l2*Z2; o=y*W+x
                    if z>zb[o]: zb[o]=z; img[o*3:o*3+3]=bytes(int(c*shade) for c in (235,225,200))
    raw=b''.join(b'\0'+bytes(img[y*W*3:(y+1)*W*3]) for y in range(H))
    ch=lambda t,dd: struct.pack('>I',len(dd))+t+dd+struct.pack('>I',zlib.crc32(t+dd))
    open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read()
    rest=skeleton.rest([d[a:a+l] for i,a,l,f in blocks(d) if i==5][0])
    meshes=[m for i,a,l,f in blocks(d) if i==3 for m in skinned_meshes(expand(d[a:a+l]))]
    e=open(sys.argv[2],'rb').read(); A,P=anim.parts([e[a:a+l] for i,a,l,f in blocks(e) if i==1][0])
    V=anim.values(A,P[1])
    fr=[int(x) for x in sys.argv[4].split(',')] if len(sys.argv)>4 else list(range(1,min(A['frames'],21),2))
    polys=[]; tris=[]
    for f in [None]+fr:
        W=[(R,t) for p,R,t in rest] if f is None else pose(rest,V[f])
        # in the body's frame (bone 1), so that the prop's flight doesn't shrink the drawing
        body=1 if len(rest)>1 else 0; back=comp((rest[body][1],rest[body][2]),inv(W[body]))
        P_=[];tris=[]
        for m in meshes:
            base=len(P_); P_+=[[x+y for x,y in zip(mv(back[0],v),back[1])] for v in skin(m,rest,W)]
            tris+=[[base+j for j in t] for t in m['tris']]
        polys.append(P_)
    stem=sys.argv[3][:-4] if sys.argv[3].endswith('.png') else sys.argv[3]
    for ax,name in (((2,1,0),'side'),((0,1,2),'front')):
        draw(polys,tris,'%s_%s.png'%(stem,name),ax=ax)
    print('rest pose and frames',fr,'->',stem+'_side.png,',stem+'_front.png')
