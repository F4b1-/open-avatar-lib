"""Textured front and side views of an avatar's stock parts, coloured from its description (PC proof, no dependencies).

Usage: python3 -I texrender.py <out.png> <AvatarAssetPack.toc> <manifest.bin>

Takes the stock items of the description's second item list (the outfit; SPECS.md 4.6) and its face features.
Colour rules (SPECS.md 3.5): clothes, hair and body lerp(base, mask.r*C1 + mask.g*C2 + mask.b*C3, mask.a), then the
decal over it by its alpha; the Head's six layers r*colour + g*white + b*skin, blended by alpha, in slot order.
"""
import sys,os,struct,math,zlib
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
from strb import blocks
from model import expand
from texture import model_textures,swap16,decode,header,HEADER,block_bytes
from material import materials

SIG=bytes.fromhex('0244ed36')
K=(2,1.632993,1.732051)
STOCK_TAIL=bytes.fromhex('c1c8f109a19cb2e0')
# Head slots in order, the component mask of the face feature that fills each, and its channel colours (r, g, b).
HEAD_SLOTS=[(0x20000,('feature 1','feature 2','skin')),(0x10000,('facial hair','white','skin')),
            (0x4000,('eyebrows','white','skin')),(0x2000,('eyes','white','skin')),(0x8000,('mouth','white','skin')),
            (0x40000,('eye shadow','black','black'))]
COLOURS=['skin','hair','mouth','eyes','eyebrows','eye shadow','facial hair','feature 1','feature 2']

def meshes(d):
    """As render.meshes, plus each vertex's texture coordinates (16-bit halves after the fixed fields)."""
    n=int.from_bytes(d,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    hdr=struct.unpack_from('<6I',d,0); q=d.find(SIG)-4; out=[]
    while q>=0 and len(out)<hdr[5]:
        nv=struct.unpack_from('<I',d,q)[0]; step,mx,my,mz=struct.unpack_from('<4f',d,q+4); s=(q+20)*8
        w=[val(s+6*k,6) for k in range(3)]; el=[val(s+18+32*k,32) for k in range(8)]
        vstart=s+274; fixed=sum(w)+el[1]+el[3]+el[5]+el[7]; iw=max(1,(nv-1).bit_length())
        for th in range(0,13):
            ib=(vstart+(fixed+16*th)*nv+7)//8; c,z,ww=struct.unpack_from('<IHH',d,ib)
            if z==0 and ww==iw and c%3==0 and c>0: break
        rec=fixed+16*th; P=[]; UV=[]
        for i in range(nv):
            b=vstart+rec*i
            q3=(val(b,w[0]),val(b+w[0],w[1]),val(b+w[0]+w[1],w[2]))
            P.append(tuple((mx,my,mz)[a]+q3[a]*step*K[a] for a in range(3)))
            h=[struct.unpack('<e',struct.pack('<H',val(b+fixed+16*j,16)))[0] for j in range(th)]
            UV.append([(h[2*j],h[2*j+1]) for j in range(th//2)])
        idx=[val((ib+8)*8+iw*k,iw) for k in range(c)]
        out.append(dict(P=P,UV=UV,tris=[idx[k:k+3] for k in range(0,c,3)]))
        p=d.find(SIG,((ib+8)*8+c*iw+7)//8); q=p-4 if p>=0 else -1
    return out

def image(hd,pixels,frame=0):
    body=swap16(pixels) if hd['fmt']>>6==1 else pixels; n=len(body)//hd['count']
    return (hd['width'],hd['height'],decode(body[frame*n:(frame+1)*n],hd['width'],hd['height'],False,hd['fmt']))

def sample(img,u,v):
    w,h,px=img; return px[int((v%1.0)*h)%h][int((u%1.0)*w)%w]

def load_model(blob):
    for bid,at,ln,fs in blocks(blob):
        if bid==3:
            m=expand(blob[at:at+ln]); ntex=struct.unpack_from('<I',m,24)[0]; ms=meshes(m)
            if not ms: return None
            from model_tex import meshes_end
            texs=[t for t in model_textures(m,meshes_end(m),ntex)][:-1]
            imgs=[image(hd,px) if px else None for _,_,hd,px in texs]
            mat={pid:v for t,pid,v in materials(m) if t==1}
            return dict(meshes=ms,imgs=imgs,bind={pid:(imgs[v[0]],v[1]) for pid,v in mat.items()})

def load_texture(blob):
    for bid,at,ln,fs in blocks(blob):
        if bid==2:
            t=expand(blob[at:at+ln]); hd=header(t); return image(hd,t[HEADER:])

def lerp(a,b,t): return tuple(x+(y-x)*t for x,y in zip(a,b))

def item_shader(model,C):
    def f(uvs):
        tex,uv=model['bind'][1]; base=sample(tex,*uvs[uv]); col=[c/255 for c in base[:3]]
        if 2 in model['bind']:
            tex,uv=model['bind'][2]; m=[c/255 for c in sample(tex,*uvs[uv])]
            tint=tuple(m[0]*C[0][i]+m[1]*C[1][i]+m[2]*C[2][i] for i in range(3)); col=lerp(col,tint,m[3])
        for pid in (3,4):
            if pid in model['bind']:
                tex,uv=model['bind'][pid]; d=[c/255 for c in sample(tex,*uvs[uv])]; col=lerp(col,d[:3],d[3])
        return col
    return f

def head_shader(model,layers,col):
    def f(uvs):
        c=col['skin']
        for slot,(img,names) in enumerate(layers):
            if img is None: continue
            _,uv=[b for pid,b in model['bind'].items()][slot]
            m=[x/255 for x in sample(img,*uvs[uv])]
            layer=tuple(sum(m[k]*col[names[k]][i] for k in range(3)) for i in range(3)); c=lerp(c,layer,m[3])
        return c
    return f

def draw(parts,size=480,out='out.png'):
    views=[(0,1,2,1),(2,1,0,-1)]
    W=size*2; H=size; img=bytearray([40,40,48]*W*H); zb=[-1e9]*(W*H)
    ys=[p[1] for ms,_ in parts for m in ms for p in m['P']]; lo,hi=min(ys),max(ys); sc=(size*0.9)/(hi-lo)
    for vi,(ax,ay,az,sg) in enumerate(views):
        ox=vi*size+size//2
        for ms,shader in parts:
            for m in ms:
                P=m['P']
                for t in m['tris']:
                    a,b,c=(P[i] for i in t)
                    u=[b[j]-a[j] for j in range(3)]; v=[c[j]-a[j] for j in range(3)]
                    nrm=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]); L=math.sqrt(sum(x*x for x in nrm)) or 1
                    shade=0.45+0.55*abs(nrm[az])/L
                    pts=[(ox+sg*p[ax]*sc,H-20-(p[ay]-lo)*sc,sg*p[az]) for p in (a,b,c)]
                    x0=max(int(min(p[0] for p in pts)),vi*size); x1=min(int(max(p[0] for p in pts))+1,vi*size+size-1)
                    y0=max(int(min(p[1] for p in pts)),0); y1=min(int(max(p[1] for p in pts))+1,H-1)
                    (X0,Y0,Z0),(X1,Y1,Z1),(X2,Y2,Z2)=pts; den=(Y1-Y2)*(X0-X2)+(X2-X1)*(Y0-Y2)
                    if abs(den)<1e-9: continue
                    UVa,UVb,UVc=(m['UV'][i] for i in t)
                    for y in range(y0,y1+1):
                        for x in range(x0,x1+1):
                            l0=((Y1-Y2)*(x-X2)+(X2-X1)*(y-Y2))/den; l1=((Y2-Y0)*(x-X2)+(X0-X2)*(y-Y2))/den; l2=1-l0-l1
                            if l0<0 or l1<0 or l2<0: continue
                            z=l0*Z0+l1*Z1+l2*Z2; o=y*W+x
                            if z>zb[o]:
                                zb[o]=z
                                uvs=[(l0*p[0]+l1*q[0]+l2*r[0],l0*p[1]+l1*q[1]+l2*r[1]) for p,q,r in zip(UVa,UVb,UVc)]
                                col=shader(uvs); img[o*3:o*3+3]=bytes(max(0,min(255,int(ch*255*shade))) for ch in col)
    raw=b''.join(b'\0'+bytes(img[y*W*3:(y+1)*W*3]) for y in range(H))
    ch=lambda t,dd: struct.pack('>I',len(dd))+t+dd+struct.pack('>I',zlib.crc32(t+dd))
    open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))

def argb(b): return (b[1]/255,b[2]/255,b[3]/255)

if __name__=='__main__':
    toc=open(sys.argv[2],'rb').read(); man=open(sys.argv[3],'rb').read()[:1000]
    recs={r['i']:r for r in records(toc)}; blob=lambda i: toc[recs[i]['blob']:recs[i]['blob']+recs[i]['size']]
    col={n:argb(man[0xFC+4*k:0x100+4*k]) for k,n in enumerate(COLOURS)}; col['white']=(1,1,1); col['black']=(0,0,0)
    # Face features: stock ids by component mask.
    face={}
    for j in range(15):
        g=man[0x0C+16*j:0x1C+16*j]
        if g[8:]==STOCK_TAIL: mask,i,_=struct.unpack('>IHH',g[:8]); face[mask]=i
    parts=[]
    for j in range(4):
        e=man[0x300+32*j:0x320+32*j]
        if e[8:16]!=STOCK_TAIL: continue
        mask,i,_=struct.unpack('>IHH',e[:8])
        if not i in recs: continue
        model=load_model(blob(i))
        if model is None: continue
        custom=[argb(e[20+4*k:24+4*k]) for k in range(3)]
        C=[col['hair']]*3 if mask==0x4 else custom
        print('item',i,cstr(toc,recs[i]['names'][0]),'colours',C); parts.append((model['meshes'],item_shader(model,C)))
    body=load_model(blob(0)); parts.append((body['meshes'],item_shader(body,[col['skin']]*3)))
    head=load_model(blob(2))
    layers=[(load_texture(blob(face[mk])) if mk in face else None,names) for mk,names in HEAD_SLOTS]
    print('head layers',[face.get(mk) for mk,_ in HEAD_SLOTS])
    parts.append((head['meshes'],head_shader(head,layers,col)))
    draw(parts,out=sys.argv[1])
