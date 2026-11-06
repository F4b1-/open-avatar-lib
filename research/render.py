"""Decode meshes fully and draw flat-shaded orthographic views to a PNG (no dependencies)."""
import sys,os,struct,zlib,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from model import expand
SIG=bytes.fromhex('0244ed36')
def meshes(d):
    n=int.from_bytes(d,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    hdr=struct.unpack_from('<6I',d,0); q=d.find(SIG)-4; out=[]
    while q>=0 and len(out)<hdr[5]:
        nv=struct.unpack_from('<I',d,q)[0]; step,mx,my,mz=struct.unpack_from('<4f',d,q+4); s=(q+20)*8
        w=[val(s+6*k,6) for k in range(3)]; el=[val(s+18+32*k,32) for k in range(8)]
        vstart=s+274; fixed=sum(w)+el[1]+el[3]+el[5]+el[7]; iw=max(1,(nv-1).bit_length())
        for th in range(0,13):
            ib=(vstart+(fixed+16*th)*nv+7)//8; c,z,ww=struct.unpack_from('<IHH',d,ib)
            if z==0 and ww==iw and c%3==0 and c>0: break
        rec=fixed+16*th
        pos=[]
        for i in range(nv):
            b=vstart+rec*i
            pos.append((val(b,w[0]),val(b+w[0],w[1]),val(b+w[0]+w[1],w[2])))
        idx=[val((ib+8)*8+iw*k,iw) for k in range(c)]
        out.append(dict(step=step,min=(mx,my,mz),q=pos,tris=[idx[k:k+3] for k in range(0,c,3)]))
        p=d.find(SIG,((ib+8)*8+c*iw+7)//8); q=p-4 if p>=0 else -1
    return out
def load(path):
    d=open(path,'rb').read()
    return [m for bid,at,ln,fs in blocks(d) if bid==3 for m in meshes(expand(d[at:at+ln]))]
def world(m,k):
    return [tuple(m['min'][a]+v[a]*m['step']*k[a] for a in range(3)) for v in m['q']]
def draw(parts,k,size=420,out='out.png'):
    views=[(0,1,2,1),(2,1,0,-1)]  # front: x,y depth z ; side: z,y depth x
    W=size*2; H=size; img=bytearray([40,40,48]*W*H); zb=[-1e9]*(W*H)
    allv=[v for ms,col in parts for m in ms for v in world(m,k)]
    ys=[v[1] for v in allv]; lo,hi=min(ys),max(ys); sc=(size*0.9)/(hi-lo)
    for vi,(ax,ay,az,sg) in enumerate(views):
        ox=vi*size+size//2
        for ms,col in parts:
            for m in ms:
                P=world(m,k)
                for t in m['tris']:
                    a,b,c=(P[i] for i in t)
                    u=[b[j]-a[j] for j in range(3)]; v=[c[j]-a[j] for j in range(3)]
                    nrm=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]); L=math.sqrt(sum(x*x for x in nrm)) or 1
                    shade=0.35+0.65*abs(nrm[az])/L
                    pts=[(ox+sg*p[ax]*sc, H-20-(p[ay]-lo)*sc, sg*p[az]) for p in (a,b,c)]
                    x0=max(int(min(p[0] for p in pts)),vi*size); x1=min(int(max(p[0] for p in pts))+1,vi*size+size-1)
                    y0=max(int(min(p[1] for p in pts)),0); y1=min(int(max(p[1] for p in pts))+1,H-1)
                    (X0,Y0,Z0),(X1,Y1,Z1),(X2,Y2,Z2)=pts; den=(Y1-Y2)*(X0-X2)+(X2-X1)*(Y0-Y2)
                    if abs(den)<1e-9: continue
                    for y in range(y0,y1+1):
                        for x in range(x0,x1+1):
                            l0=((Y1-Y2)*(x-X2)+(X2-X1)*(y-Y2))/den; l1=((Y2-Y0)*(x-X2)+(X0-X2)*(y-Y2))/den; l2=1-l0-l1
                            if l0<0 or l1<0 or l2<0: continue
                            z=l0*Z0+l1*Z1+l2*Z2; o=y*W+x
                            if z>zb[o]: zb[o]=z; img[o*3:o*3+3]=bytes(int(c*shade) for c in col)
    raw=b''.join(b'\0'+bytes(img[y*W*3:(y+1)*W*3]) for y in range(H))
    ch=lambda t,dd: struct.pack('>I',len(dd))+t+dd+struct.pack('>I',zlib.crc32(t+dd))
    open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
if __name__=='__main__':
    k=tuple(float(x) for x in sys.argv[2].split(','))
    cols=[(220,180,150),(200,170,140),(90,60,40),(70,110,200)]
    parts=[(load(p),cols[i%4]) for i,p in enumerate(sys.argv[3:])]
    draw(parts,k,out=sys.argv[1])
