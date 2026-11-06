import sys,os,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from anim import parse
def frames(path,k=(2.0,2.0,2.0)):
    d=open(path,'rb').read(); b=[d[a:a+l] for i,a,l,f in blocks(d) if i==1][0]
    A=parse(b); ch=A['channels']; val=A['val']; pos=A['data_bit']; step=7.0710677e-06
    out=[]
    for f in range(A['frames']):
        vals=[]
        for c in ch:
            v=[]
            for j in range(3):
                q=val(pos,c['w'][j]) if c['w'][j] else 0; pos+=c['w'][j]
                v.append(c['min'][j]+q*step*k[j])
            vals.append(v)
        out.append(vals)
    return A,out
if __name__=='__main__':
    NAMES={0:'Root',1:'BackLower',2:'HipLeft',3:'HipRight',5:'BackUpper',6:'KneeLeft',8:'KneeRight',11:'AnkleLeft',12:'CollarLeft',14:'Neck',15:'AnkleRight',16:'CollarRight',19:'Head',20:'ShoulderLeft',21:'ToeLeft',22:'ShoulderRight',23:'ToeRight',25:'ElbowLeft',28:'ElbowRight',33:'WristLeft',36:'WristRight',41:'PropLeft',42:'SpecialLeft',48:'PropRight',49:'SpecialRight'}
    A,F=frames(sys.argv[1])
    rows=[]
    for bone in range(71):
        c=3*bone+1
        span=max(max(f[c][j] for f in F)-min(f[c][j] for f in F) for j in range(3))
        mx=max(math.sqrt(sum(x*x for x in f[c])) for f in F)
        rows.append((span,bone,mx))
    rows.sort(reverse=True)
    for span,bone,mx in rows[:16]: print(f"bone {bone:2d} {NAMES.get(bone,''):13} rotation span {span:.3f}  max |xyz| {mx:.3f}")
    print('max |xyz| over all bones and frames', max(r[2] for r in rows))
