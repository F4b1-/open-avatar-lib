"""AvatarAssetPack.toc reader (version 1 and 2). Big-endian."""
import sys,struct
def be(d,o): return struct.unpack_from('>I',d,o)[0]
def cstr(d,o):
    if not o: return None
    e=o
    while e+1<len(d) and d[e:e+2]!=b'\0\0': e+=2
    try: return d[o:e].decode('utf-16-be')
    except UnicodeDecodeError: return repr(d[o:e][:40])
def layout(d):
    """(first record, locale count). Version 2: records at 0x30, locales at 0x28. Version 1: 13 locales, records at
    0x20 (All-Stars Avatar's disc copy) or 0x28 (the console's AvatarAssetPackLegacyV1.toc): the one whose first
    record points at an STRB blob."""
    ver=be(d,0x18)
    if ver!=1: return 0x30,be(d,0x28)
    for first in (0x20,0x28):
        blob=be(d,first+0xBC+4*13)
        if blob+4<=len(d) and d[blob:blob+4]==b'STRB': return first,13
    raise ValueError('no version 1 layout fits')
def records(d):
    count=be(d,0x1c); first,loc=layout(d); stride=8+0xB4+4*loc+8
    for i in range(count):
        b=first+i*stride
        names=[be(d,b+0xBC+4*k) for k in range(loc)]
        blob,size=be(d,b+0xBC+4*loc),be(d,b+0xBC+4*loc+4)
        yield dict(i=i,at=b,head=d[b:b+0xBC],names=names,blob=blob,size=size)
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read()
    print('version',be(d,0x18),'count',be(d,0x1c),'layout',layout(d),'hdr',d[:0x30].hex(' '))
    n=int(sys.argv[2]) if len(sys.argv)>2 else 5
    for r in list(records(d))[:n]:
        print(f"#{r['i']} @{r['at']:#x} blob {r['blob']:#x} size {r['size']:#x} name0 {cstr(d,r['names'][0])!r}")
