import sys,collections
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
from strb import blocks
d=open(sys.argv[1],'rb').read()
kinds=collections.Counter(); noblob=0; rows=[]
for r in records(d):
    name=cstr(d,r['names'][0]); h=r['head']
    if not r['blob']: noblob+=1; rows.append((r['i'],name,'-',h[:8].hex(),h[8:0x18].hex())); continue
    blob=d[r['blob']:r['blob']+r['size']]
    try: ids=[b[0] for b in blocks(blob)]
    except Exception as e: ids=['ERR']
    meta=None
    for bid,at,ln,fs in blocks(blob):
        if bid==8: meta=blob[at:at+ln]
    kinds[tuple(ids)]+=1
    rows.append((r['i'],name,ids,h[:8].hex(),h[8:0x18].hex(), meta.hex() if meta else ''))
print('records',len(rows),'without blob',noblob)
for k,v in kinds.most_common(): print('  blocks',k,v)
for row in rows:
    if any(x in (row[2] if isinstance(row[2],list) else []) for x in (5,1)) or row[0]<12:
        print(row)
open(sys.argv[2],'w').write('\n'.join(repr(r) for r in rows))
