import sys,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from toc import records,cstr
d=open(sys.argv[1],'rb').read(); out=sys.argv[2]; os.makedirs(out,exist_ok=True)
want=set(int(x) for x in sys.argv[3].split(','))
for r in records(d):
    if r['i'] in want and r['blob']:
        open(f"{out}/{r['i']:04d}.bin",'wb').write(d[r['blob']:r['blob']+r['size']]); print(r['i'],cstr(d,r['names'][0]))
