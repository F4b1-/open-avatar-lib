import sys,glob,os,struct
SIG=bytes.fromhex('0244ed36')
class B:
    def __init__(s,d): s.d=d
    def val(s,pos,w):
        v=0
        for k in range(w):
            q=pos+k; v|=((s.d[q>>3]>>(q&7))&1)<<k
        return v
def mesh_at(d,q):
    b=B(d); nv=struct.unpack_from('<I',d,q)[0]; step,mx,my,mz=struct.unpack_from('<4f',d,q+4)
    s=(q+20)*8; widths=[b.val(s+6*k,6) for k in range(3)]
    el=[b.val(s+18+32*k,32) for k in range(8)]
    vstart=s+18+256; fixed=sum(widths)+el[1]+el[3]+el[5]+el[7]
    iw=max(1,(nv-1).bit_length()); hits=[]
    for th in range(1,13):
        rec=fixed+16*th; ib=(vstart+rec*nv+7)//8
        if ib+8>len(d): continue
        cnt,z,w=struct.unpack_from('<IHH',d,ib)
        if z==0 and w==iw and cnt%3==0 and cnt>0 and (ib+8)*8+cnt*w<=len(d)*8: hits.append((th,rec,ib,cnt))
    th,rec,ib,cnt=hits[0]
    idx=[b.val((ib+8)*8+iw*k,iw) for k in range(cnt)]
    pos=[[b.val(vstart+rec*i+sum(widths[:a]),widths[a]) for a in range(3)] for i in range(nv)]
    end=((ib+8)*8+cnt*iw+7)//8
    return dict(nv=nv,min=(mx,my,mz),widths=widths,bits=el[1::2],tex_halves=th,cands=len(hits),tris=cnt//3,maxidx=max(idx),end=end,pos=pos,idx=idx)
if __name__=='__main__':
    for f in sorted(glob.glob(sys.argv[1]+'/*.model')):
        d=open(f,'rb').read(); hdr=struct.unpack_from('<22I',d,0)
        print(os.path.basename(f)[:16],'len',len(d),'hdr',[hex(x) for x in hdr[:8]])
        p=d.find(SIG); tot=0
        while p>=4:
            m=mesh_at(d,p-4); tot+=m['tris']*3
            print(f"   mesh@{p-4:#x} verts {m['nv']} tris {m['tris']} maxidx {m['maxidx']} posbits {m['widths']} n/w/i/c bits {m['bits']} uv halves {m['tex_halves']} cands {m['cands']} min {tuple(round(x,3) for x in m['min'])} end {m['end']:#x}")
            p=d.find(SIG,m['end'])
        print('   index bytes',tot*2,'hdr[4]',hdr[4],'meshes hdr[5]',hdr[5])
