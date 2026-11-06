"""Decode an expanded texture block (research/model.py writes <name>.texture) to PNG files, one per frame.

Usage: python3 -I texture.py <name>.texture <out prefix> [linear|tiled]
Writes <out prefix>_<frame>.png (RGBA). Prints the header words.
"""
import sys,struct,zlib

HEADER=33

def header(d):
    """The 33-byte header, little-endian. Field names are inferred (see SPECS.md 3.4)."""
    fmt=d[0]; flags=d[1]; tag=struct.unpack_from('<H',d,2)[0]
    w,h,total,frame,count,unk,pitch=struct.unpack_from('<7I',d,4)
    return dict(fmt=fmt,flags=flags,tag=tag,width=w,height=h,total=total,frame=frame,count=count,unk=unk,pitch=pitch,
                last=d[32])

def swap16(b):
    """Endian mode 1 (8-in-16): swap the bytes of each 16-bit word."""
    a=bytearray(b); a[0::2],a[1::2]=b[1::2],b[0::2]; return bytes(a)

def tiled_block(x,y,width_blocks,log_bpp):
    """Block index of block (x, y) in a Xenos-tiled 2D surface (the tiling formula as published in xenia, BSD)."""
    aw=(width_blocks+31)&~31
    macro=((x>>5)+(y>>5)*(aw>>5))<<(log_bpp+7)
    micro=((x&7)+((y&6)<<2))<<log_bpp
    off=macro+((micro&~15)<<1)+(micro&15)+((y&8)<<(3+log_bpp))+((y&1)<<4)
    return (((off&~511)<<3)+((off&448)<<2)+(off&63)+((y&16)<<7)+(((((y&8)>>2)+(x>>3))&3)<<6))>>log_bpp

def rgb565(c):
    r=(c>>11)&31; g=(c>>5)&63; b=c&31
    return ((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))

def dxt5_block(b):
    a0,a1=b[0],b[1]; abits=int.from_bytes(b[2:8],'little')
    al=[a0,a1]+([((6-i)*a0+i*a1)//6 for i in range(1,7)] if a0>a1 else [((5-i)*a0+i*a1)//5 for i in range(1,5)]+[0,255])
    c0,c1=struct.unpack_from('<HH',b,8); cbits=struct.unpack_from('<I',b,12)[0]
    p0,p1=rgb565(c0),rgb565(c1)
    cols=[p0,p1,tuple((2*u+v)//3 for u,v in zip(p0,p1)),tuple((u+2*v)//3 for u,v in zip(p0,p1))]
    return [cols[(cbits>>(2*i))&3]+(al[(abits>>(3*i))&7],) for i in range(16)]

def dxt1_block(b):
    c0,c1,cbits=struct.unpack_from('<HHI',b,0); p0,p1=rgb565(c0),rgb565(c1)
    if c0>c1: cols=[p0+(255,),p1+(255,),tuple((2*u+v)//3 for u,v in zip(p0,p1))+(255,),tuple((u+2*v)//3 for u,v in zip(p0,p1))+(255,)]
    else: cols=[p0+(255,),p1+(255,),tuple((u+v)//2 for u,v in zip(p0,p1))+(255,),(0,0,0,0)]
    return [cols[(cbits>>(2*i))&3] for i in range(16)]

def block_bytes(fmt): return 16 if fmt&0x3F==0x14 else 8

def decode(data,w,h,tiled,fmt=0x14):
    bw,bh=w//4,h//4; px=[[(0,0,0,0)]*w for _ in range(h)]; bb=block_bytes(fmt)
    for by in range(bh):
        for bx in range(bw):
            i=tiled_block(bx,by,bw,4 if bb==16 else 3) if tiled else by*bw+bx
            blk=(dxt5_block if bb==16 else dxt1_block)(data[i*bb:i*bb+bb])
            for k,p in enumerate(blk): px[by*4+k//4][bx*4+k%4]=p
    return px

def png(path,px):
    h=len(px); w=len(px[0])
    raw=b''.join(b'\0'+bytes(c for p in row for c in p) for row in px)
    ch=lambda t,dd: struct.pack('>I',len(dd))+t+dd+struct.pack('>I',zlib.crc32(t+dd))
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))
                          +ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))

def model_textures(d,at,count):
    """The texture table after a model's last mesh: per texture u32 offset and u32 allocated size (into the model's
    texture memory, header word 2), the 33-byte header, then the pixels unless bit 0 of the header's 'unk' word is
    set (an empty slot, as all six of the Head's). Yields (offset, size, header, pixels or None)."""
    for _ in range(count):
        off,size=struct.unpack_from('<II',d,at); hd=header(d[at+8:]); at+=8+HEADER; px=None
        if not hd['unk']&1:
            n=hd['width']*hd['height']//16*block_bytes(hd['fmt'])*hd['count']; px=d[at:at+n]; at+=n
        yield off,size,hd,px
    yield at,None,None,None

def write_frames(prefix,hd,pixels,tiled=False):
    body=swap16(pixels) if hd['fmt']>>6==1 else pixels
    n=len(body)//hd['count']
    for f in range(hd['count']):
        png(f"{prefix}_{f}.png",decode(body[f*n:(f+1)*n],hd['width'],hd['height'],tiled,hd['fmt']))

if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); hd=header(d); print(hd, 'data', len(d)-HEADER)
    write_frames(sys.argv[2],hd,d[HEADER:],len(sys.argv)>3 and sys.argv[3]=='tiled')
