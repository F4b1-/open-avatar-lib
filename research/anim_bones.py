import sys,os,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from anim import parse
NAMES={0:'Root',1:'BackLower',2:'HipLeft',3:'HipRight',5:'BackUpper',6:'KneeLeft',8:'KneeRight',11:'AnkleLeft',12:'CollarLeft',14:'Neck',15:'AnkleRight',16:'CollarRight',19:'Head',20:'ShoulderLeft',21:'ToeLeft',22:'ShoulderRight',23:'ToeRight',25:'ElbowLeft',28:'ElbowRight',33:'WristLeft',36:'WristRight',37:'IndexL',41:'PropLeft',42:'SpecialLeft',43:'ThumbL',44:'IndexR',48:'PropRight',49:'SpecialRight',50:'ThumbR'}
d=open(sys.argv[1],'rb').read(); b=[d[a:a+l] for i,a,l,f in blocks(d) if i==1][0]
ch=parse(b)['channels']
for bone in range(71):
    R,T,S=ch[3*bone:3*bone+3]
    t=T['min']; L=math.sqrt(sum(x*x for x in t))
    print(f"{bone:2d} {NAMES.get(bone,''):13} T ({t[0]:+.4f},{t[1]:+.4f},{t[2]:+.4f}) |T| {L:.4f} wT {T['w']}  R {['%.2g'%x for x in R['min']]} wR {R['w']}  S {['%.3g'%x for x in S['min']]} wS {S['w']}")
