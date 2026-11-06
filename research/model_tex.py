"""The texture table after a model's meshes: list it, check where it ends, write each texture as PNG.

Usage: python3 -I model_tex.py <out dir for PNGs> <name>.model ...
"""
import sys,os,struct
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from mesh import mesh_at
from texture import model_textures,write_frames

STEP=b'\x02\x44\xed\x36'

def meshes_end(d):
    q=d.find(STEP); end=0
    while q>=0:
        end=mesh_at(d,q-4)['end']; q=d.find(STEP,end)
    return end

if __name__=='__main__':
    out=sys.argv[1]
    for f in sys.argv[2:]:
        d=open(f,'rb').read(); words=struct.unpack_from('<8I',d,0); name=os.path.basename(f).split('.')[0]
        at=meshes_end(d)
        print(name,'textures',words[6],'memory',hex(words[2]),'table at',hex(at))
        for i,(off,size,hd,px) in enumerate(model_textures(d,at,words[6])):
            if hd is None:
                print('  table ends',hex(off),'file',hex(len(d)),'rest',d[off:].hex(' ')[:120]); break
            print('  %d: @%#x size %#x fmt %#x %dx%d x%d unk %#x %s'%(i,off,size,hd['fmt'],hd['width'],hd['height'],
                  hd['count'],hd['unk'],'slot' if px is None else '%d bytes'%len(px)))
            if px: write_frames(f"{out}/{name}_t{i}",hd,px)
