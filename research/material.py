"""The material records between the model header and the first mesh, surveyed over the whole pack.

Records start at 0x51 and run to the first mesh, 24 bytes each: u32 type, u32 parameter id, 16 bytes of value.
Type 3: a float4. Type 1: a texture binding, u16 texture index, u16 UV set, u32 (3 or 0), 8 zero bytes.

Usage: python3 -I material.py AvatarAssetPack.toc
"""
import sys,os,struct,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
from strb import blocks
from model import expand

FIRST=0x51
STEP=b'\x02\x44\xed\x36'

def records_between(m,start,end):
    out=[]
    assert (end-start)%24==0,(hex(start),hex(end))
    for p in range(start,end,24):
        t,pid=struct.unpack_from('<II',m,p)
        if t==3: out.append((t,pid,struct.unpack_from('<4f',m,p+8)))
        else: out.append((t,pid,struct.unpack_from('<HHIII',m,p+8)))
    return out

def materials(m):
    """The first part's records (most models have one part)."""
    return records_between(m,FIRST,m.find(STEP)-4)

def parts(m):
    """[(technique, records, mesh count)]: a model is a series of parts, each a 33-byte header (its first word the
    technique) and 24-byte records, then its meshes; the first part's header is at 0x30, a later part's fills the gap
    before its first mesh (FORMATS.md, section 3.6)."""
    from mesh import mesh_at
    out=[]; q=m.find(STEP)-4; header=0x30; first=q; meshes=0
    while q>=0:
        end=mesh_at(m,q)['end']; meshes+=1
        nq=m.find(STEP,end); nq=nq-4 if nq>=0 else -1
        if nq<0 or nq>end:
            out.append((struct.unpack_from('<I',m,header)[0],records_between(m,header+33,first),meshes))
            header=end; first=nq; meshes=0
        q=nq
    return out

if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); ids=collections.Counter(); vals=collections.Counter(); sets=collections.Counter()
    bad=0; ex={}
    for r in records(d):
        if not r['blob']: continue
        blob=d[r['blob']:r['blob']+r['size']]
        for bid,at,ln,fs in blocks(blob):
            if bid!=3: continue
            m=expand(blob[at:at+ln]); ntex=struct.unpack_from('<I',m,24)[0]
            try: mat=materials(m)
            except AssertionError as e: bad+=1; print('ALIGN',r['i'],cstr(d,r['names'][0]),e); continue
            for t,pid,v in mat:
                ids[(t,pid)]+=1
                if t==3: vals[(pid,v)]+=1
                else:
                    vals[(pid,'mode',v[2])]+=1
                    if v[0]>=ntex or v[3] or v[4]: bad+=1; print('TEX',r['i'],cstr(d,r['names'][0]),pid,v,ntex)
            key=tuple(p for t,p,_ in mat); sets[key]+=1; ex.setdefault(key,cstr(d,r['names'][0]))
    print('bad',bad)
    print('records by (type, id):',sorted(ids.items()))
    for k,v in sorted(vals.items(),key=str): print(' ',v,k)
    for k,v in sets.most_common(): print('set',v,[hex(x) for x in k],'e.g.',ex[k])
