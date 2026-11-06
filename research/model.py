import sys,glob,os,struct
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from lzx import Lzx, LzxError
def chunks(blk):
    """Chunks of a model or texture block: u32 compressed size, u32 output offset, u32 output size, then data."""
    p=0
    while p+12<=len(blk):
        cs,off,us=struct.unpack_from('<3I',blk,p); yield off,us,blk[p+12:p+12+cs]; p+=12+cs
    assert p==len(blk),(p,len(blk))
def expand(blk):
    """Each chunk is an LZX stream of its own: 32 KB window, its own header, trees and repeated offsets."""
    out=bytearray()
    for off,us,data in chunks(blk):
        assert off==len(out)
        o,_=Lzx(15).decompress(data,us); out+=o
    return bytes(out)
if __name__=='__main__':
    os.makedirs(sys.argv[2],exist_ok=True)
    for f in sorted(glob.glob(sys.argv[1]+'/*.bin')):
        d=open(f,'rb').read(); name=os.path.basename(f)[:-4]
        for bid,at,ln,fs in blocks(d):
            if bid in (2,3):
                out=expand(d[at:at+ln])
                open(f"{sys.argv[2]}/{name}.{'model' if bid==3 else 'texture'}",'wb').write(out)
                print(name,'block',bid,'chunks',[c[:2] for c in chunks(d[at:at+ln])],'->',len(out))
