import sys,os,struct
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from model import expand
SIG=bytes.fromhex('0244ed36')
def bone_info(d):
    n=int.from_bytes(d,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    hdr=struct.unpack_from('<6I',d,0); q=d.find(SIG)-4; res=[]
    while q>=0 and len(res)<hdr[5]:
        nv=struct.unpack_from('<I',d,q)[0]; s=(q+20)*8
        w=[val(s+6*k,6) for k in range(3)]; el=[val(s+18+32*k,32) for k in range(8)]
        vstart=s+274; fixed=sum(w)+el[1]+el[3]+el[5]+el[7]; iw=max(1,(nv-1).bit_length())
        for th in range(0,13):
            ib=(vstart+(fixed+16*th)*nv+7)//8; c,z,ww=struct.unpack_from('<IHH',d,ib)
            if z==0 and ww==iw and c%3==0 and c>0: break
        rec=fixed+16*th; off_i=sum(w)+el[1]+el[3]; bones=set(); wsum=set()
        for i in range(nv):
            b=vstart+rec*i
            raw=(val(b+off_i,el[5])+el[4])&0xffffffff
            ws=(val(b+sum(w)+el[1],el[3])+el[2])&0xffffffff
            bb=[(raw>>(8*k))&255 for k in range(4)]; ww4=[(ws>>(8*k))&255 for k in range(4)]
            for x,y in zip(bb,ww4):
                if y: bones.add(x)
            wsum.add(sum(ww4))
        res.append((nv,sorted(bones),sorted(wsum)[:5],hex(el[4]),hex(el[2])))
        p=d.find(SIG,((ib+8)*8+c*iw+7)//8); q=p-4 if p>=0 else -1
    return res
for f in sys.argv[1:]:
    d=open(f,'rb').read()
    for bid,at,ln,fs in blocks(d):
        if bid==3:
            for nv,b,ws,ib,wb in bone_info(expand(d[at:at+ln])):
                print(os.path.basename(f),nv,'verts; bones used',len(b),'min',b[:1],'max',b[-1:],'weight sums',ws,'idx base',ib,'w base',wb)
