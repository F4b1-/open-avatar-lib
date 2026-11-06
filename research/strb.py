import sys,struct,glob,os
NAMES={1:'anim',2:'texture',3:'model',4:'shapeovr',5:'skeleton',6:'meta1',7:'colors',8:'meta'}
def rnd(v,a): return (v+a-1)&~(a-1)
def blocks(d):
    assert d[:4]==b'STRB'
    stored,le=d[4],d[5]; e='<' if le else '>'
    idsz,spsz,al=d[0x16],d[0x17],(d[0x1a] if stored else 1)
    hsz=rnd(idsz+2*spsz,al); pos=rnd(30 if stored else 0x1a,al)
    out=[]
    while pos+hsz<=len(d):
        rd=lambda o,n:int.from_bytes(d[o:o+n],'little' if le else 'big')
        bid=rd(pos,idsz); ln=rd(pos+idsz,spsz); fs=rd(pos+idsz+spsz,spsz)
        out.append((bid,pos+hsz,ln,fs)); pos=pos+hsz+rnd(ln,al)
    return out
if __name__=='__main__':
    for f in sorted(glob.glob(sys.argv[1]+'/*.bin')):
        d=open(f,'rb').read()
        print(os.path.basename(f)[:16],hex(len(d)))
        for bid,at,ln,fs in blocks(d):
            print(f"   id {bid} {NAMES.get(bid,'?'):9} data@{at:#06x} len {ln:#07x} field {fs}  {d[at:at+24].hex(' ')}")
