"""Every texture block in the pack: expand it, check the header against the data, count header variants.

Usage: python3 -I tex_survey.py AvatarAssetPack.toc
"""
import sys,os,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
from strb import blocks
from model import expand
from texture import header,HEADER

d=open(sys.argv[1],'rb').read(); seen=collections.Counter(); bad=0; n=0
for r in records(d):
    if not r['blob']: continue
    blob=d[r['blob']:r['blob']+r['size']]
    for bid,at,ln,fs in blocks(blob):
        if bid!=2: continue
        t=expand(blob[at:at+ln]); h=header(t); n+=1
        bpb=16 if h['fmt']&0x3F==0x14 else 8
        ok=(len(t)-HEADER==h['total']==h['frame']*h['count'] and h['frame']==h['width']*h['height']//16*bpb)
        if not ok:
            bad+=1; print('MISMATCH',r['i'],cstr(d,r['names'][0]),h,len(t)-HEADER)
        seen[(hex(h['fmt']),h['flags'],hex(h['tag']),h['width'],h['height'],hex(h['unk']),h['pitch'],h['last'])]+=1
        seen[('count',h['count'])]+=1
print(n,'textures,',bad,'mismatches')
for k,v in sorted(seen.items(),key=str): print(v,k)

# Models: walk each model's texture table (model_tex.py) and check it against header words 2 and 6.
from model_tex import meshes_end
from texture import model_textures
import struct
mods=0; fails=0; after=collections.Counter(); kinds=collections.Counter()
for r in records(d):
    if not r['blob']: continue
    blob=d[r['blob']:r['blob']+r['size']]
    for bid,at,ln,fs in blocks(blob):
        if bid!=3: continue
        m=expand(blob[at:at+ln]); mods+=1; w=struct.unpack_from('<8I',m,0)
        try:
            ents=list(model_textures(m,meshes_end(m),w[6])); end=ents[-1][0]; ents=ents[:-1]
            mem=max([o+s for o,s,_,_ in ents],default=0)
            ok=all(h['fmt']&0x3F in (0x12,0x14) and h['fmt']>>6==1 for _,_,h,_ in ents) and end<=len(m)
            for _,_,h,px in ents: kinds[(hex(h['fmt']),h['width'],h['height'],h['count'],'slot' if px is None else 'data')]+=1
            after[(m[end:end+4].hex(), len(m)-end>0)]+=1
            if not ok or (ents and (mem+0xfff)&~0xfff!=(w[2]+0xfff)&~0xfff): fails+=1; print('MODEL',r['i'],cstr(d,r['names'][0]),hex(mem),hex(w[2]))
        except Exception as e:
            fails+=1; print('MODEL',r['i'],cstr(d,r['names'][0]),'error',e)
print(mods,'models,',fails,'failures')
for k,v in sorted(kinds.items(),key=str): print(v,k)
print('after the table:',after.most_common(8))
