import sys,os,struct
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
from strb import blocks
SIG=bytes.fromhex('0244ed36')
d=open(sys.argv[1],'rb').read()
for r in records(d):
    if not r['blob']: continue
    blob=d[r['blob']:r['blob']+r['size']]
    for bid,at,ln,fs in blocks(blob):
        if bid!=1: continue
        b=blob[at:at+ln]
        h=struct.unpack_from('<IfIIIIIIIIII',b,0)
        n=0
        while b[0x30+73*n:0x34+73*n]==SIG: n+=1
        end=0x30+73*n
        print(f"{r['i']:4d} {cstr(d,r['names'][0])[:34]:34} frames {h[0]:4d} fps {h[1]:.4g} n {h[2]} [{h[3]},{h[4]},{h[5]}] offs {h[6]:#x} {h[7]:#x} {h[8]:#x} {h[9]:#x} | {h[10]},{h[11]} | recs {n} end {end:#x} len {len(b):#x}")
