"""Validate every model in an AvatarAssetPack.toc: LZX-expand, walk meshes, check counts against the model header."""
import sys,os,struct,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
from strb import blocks
from model import expand
SIG=bytes.fromhex('0244ed36')
def walk(d):
    n=int.from_bytes(d,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    hdr=struct.unpack_from('<6I',d,0); q=d.find(SIG)-4; meshes=[]; tot=0
    while q>=0 and len(meshes)<hdr[5]:
        nv=struct.unpack_from('<I',d,q)[0]; s=(q+20)*8
        w=[val(s+6*k,6) for k in range(3)]; el=[val(s+18+32*k,32) for k in range(8)]
        vstart=s+274; fixed=sum(w)+el[1]+el[3]+el[5]+el[7]; iw=max(1,(nv-1).bit_length()); hit=None
        for th in range(0,13):
            ib=(vstart+(fixed+16*th)*nv+7)//8
            if ib+8>len(d): break
            c,z,ww=struct.unpack_from('<IHH',d,ib)
            if z==0 and ww==iw and c%3==0 and c>0 and (ib+8)*8+c*iw<=len(d)*8:
                if hit: return None,'ambiguous'
                hit=(th,ib,c)
        if not hit: return None,f'no index header (mesh {len(meshes)}, {nv} verts)'
        th,ib,c=hit; end=((ib+8)*8+c*iw+7)//8
        meshes.append((nv,c//3,th,el[3]>0)); tot+=c
        p=d.find(SIG,end); q=p-4 if p>=0 else -1
    if len(meshes)!=hdr[5]: return None,f'{len(meshes)} meshes, header {hdr[5]}'
    if tot*2!=hdr[4]: return None,f'index bytes {tot*2} != {hdr[4]}'
    return meshes,'ok'
d=open(sys.argv[1],'rb').read()
res=collections.Counter(); fails=[]; uv=collections.Counter(); skinned=0; total=0
for r in records(d):
    if not r['blob']: continue
    blob=d[r['blob']:r['blob']+r['size']]
    for bid,at,ln,fs in blocks(blob):
        if bid!=3: continue
        total+=1
        try:
            m,why=walk(expand(blob[at:at+ln]))
        except Exception as e:
            m,why=None,f'{type(e).__name__}: {e}'
        res['ok' if m else 'fail']+=1
        if m:
            for nv,tris,th,sk in m: uv[th]+=1; skinned+=sk
        else: fails.append((r['i'],cstr(d,r['names'][0]),why))
print('models',total,dict(res)); print('uv halves per mesh',dict(sorted(uv.items()))); print('skinned meshes',skinned)
for f in fails[:30]: print('FAIL',f)
