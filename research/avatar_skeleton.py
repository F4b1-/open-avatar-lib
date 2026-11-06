"""The avatar's skeleton, derived (SPECS.md 4.4): no block holds it, so it is built from what the data gives and
checked by posing a body through stock animations.

- Parents: the public XNA bone names (AvatarBone, 55 of 71) and its rule "bone indices are sorted by depth", the
  unnamed bones placed by where their skin sits (thigh, shin, spine, chest, upper arm, forearm). The table below
  satisfies the depth rule exactly.
- Rest orientation: the identity for every bone (the body is in a T-pose; bones keep the world axes: x to the
  avatar's left, y up, z forward). The animations' hinges say so: knees and toes turn about x, elbows about y,
  fingers about z, the twist bones about x only.
- Rest position: for each bone, the weighted centre of the vertices its subtree shares with the rest of the body
  (skin weights of MaleBody or FemaleBody). The root sits midway between the hip joints. Bones that no animation
  moves (the skinned helpers) only follow their parent, so their position doesn't matter; the prop and special
  bones (41, 42, 48, 49) have no skin and are placed at the wrist for now.

  python3 -I avatar_skeleton.py <body .bin>                                   # the skeleton
  python3 -I avatar_skeleton.py <body .bin> <animation .bin> out f,f,...       # pose the body, draw front and side
  python3 -I avatar_skeleton.py <body .bin> --score <dir of animation .bin>    # Clap, Wave, Stand checks

Body .bin: dump_blobs.py records 0 (MaleBody) or 1 (FemaleBody). Animations: records 3-43 and the Generic Idle and
Sleep records (catalog.py); --score expects 0006.bin (Clap), 0007.bin (Wave), 0003.bin (Stand 0) in the directory.
"""
import sys,os,math,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
from model import expand
import anim,skeleton,pose as PZ
NAMES={0:'Root',1:'BackLower',2:'HipLeft',3:'HipRight',5:'BackUpper',6:'KneeLeft',8:'KneeRight',11:'AnkleLeft',
       12:'CollarLeft',14:'Neck',15:'AnkleRight',16:'CollarRight',19:'Head',20:'ShoulderLeft',21:'ToeLeft',
       22:'ShoulderRight',23:'ToeRight',25:'ElbowLeft',28:'ElbowRight',33:'WristLeft',36:'WristRight',
       41:'PropLeft',42:'SpecialLeft',48:'PropRight',49:'SpecialRight'}
PARENT={1:0,2:0,3:0,4:0,                       # 4: pelvis skin
        5:1,6:2,7:2,8:3,9:3,10:1,              # 7/9 thigh skin, 10 lower spine skin
        11:6,12:5,13:6,14:5,15:8,16:5,17:8,18:5,   # 13/17 shin skin, 18 chest skin
        19:14,20:12,21:11,22:16,23:15,24:14,   # 24 neck skin
        25:20,26:20,27:20,28:22,29:22,30:22,   # 26/29 upper arm skin by the elbow, 27/30 upper arm twist
        31:25,32:25,33:25,34:28,35:28,36:28}   # 31/34 forearm twist, 32/35 forearm skin
for hand,first in ((33,37),(36,44)):
    for i in range(7): PARENT[first+i]=hand    # index, middle, ring, small, prop, special, thumb
for j1,j2,j3 in ((37,51,61),(38,52,62),(39,53,63),(40,54,64),(43,55,65),
                 (44,56,66),(45,57,67),(46,58,68),(47,59,69),(50,60,70)):
    PARENT[j2]=j1; PARENT[j3]=j2
def depth(b): return 0 if b==0 else 1+depth(PARENT[b])
def load_body(path):
    d=open(path,'rb').read()
    return [m for i,a,l,f in blocks(d) if i==3 for m in PZ.skinned_meshes(expand(d[a:a+l]))]
def positions(meshes):
    kids=collections.defaultdict(list)
    for c,p in PARENT.items(): kids[p].append(c)
    def sub(b):
        s={b}
        for c in kids[b]: s|=sub(c)
        return s
    edge=collections.defaultdict(lambda:[0.0,0.0,0.0,0.0])
    for m in meshes:
        for p,bi,w in m['v']:
            for i in range(4):
                for j in range(i+1,4):
                    if w[i]>0 and w[j]>0 and bi[i]!=bi[j]:
                        x=min(w[i],w[j]); e=edge[tuple(sorted((bi[i],bi[j])))]
                        for a in range(3): e[a]+=p[a]*x
                        e[3]+=x
    P={}
    for b in range(1,71):
        S=sub(b); acc=[0.0]*4
        for (u,v),e in edge.items():
            if (u in S)!=(v in S):
                for a in range(4): acc[a]+=e[a]
        P[b]=[acc[a]/acc[3] for a in range(3)] if acc[3] else None
    P[0]=[(P[2][a]+P[3][a])/2 for a in range(3)]
    for b in range(1,71):
        if P[b] is None: P[b]=P[PARENT[b]]
    return P
def rotation(v):
    """An animation's rotation value: a quaternion logarithm (axis times half the angle)."""
    return skeleton.rotvec([2*x for x in v])
def pose(P,vals):
    """World transforms (R, t) of the 71 bones for one frame's (T, R, S) values."""
    W={}
    for b in range(71):
        T,R,Sv=vals[b]
        M=PZ.mm(rotation(R),[[Sv[0],0,0],[0,Sv[1],0],[0,0,Sv[2]]])
        if b==0: W[0]=(M,[P[0][a]+T[a] for a in range(3)])
        else:
            p=PARENT[b]; W[b]=PZ.comp(W[p],(M,[P[b][a]-P[p][a]+T[a] for a in range(3)]))
    return W
def skin(meshes,P,W):
    S={b:(W[b][0],[W[b][1][a]-sum(W[b][0][a][k]*P[b][k] for k in range(3)) for a in range(3)]) for b in W}
    out=[]
    for m in meshes:
        for p,bi,w in m['v']:
            v=[0.0,0.0,0.0]
            for k in range(4):
                if w[k]>0:
                    R,t=S[bi[k]]; q=PZ.mv(R,p)
                    for a in range(3): v[a]+=w[k]*(q[a]+t[a])
            out.append(v)
    return out
def load_anim(path):
    e=open(path,'rb').read(); A,parts=anim.parts([e[a:a+l] for i,a,l,f in blocks(e) if i==1][0])
    return A,anim.values(A,parts[0])
def score(meshes,P,d):
    dist=lambda a,b: math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
    A,V=load_anim(os.path.join(d,'0006.bin'))
    clap=min(dist(W[33][1],W[36][1]) for W in (pose(P,V[f]) for f in range(A['frames'])))
    A,V=load_anim(os.path.join(d,'0007.bin'))
    wave=max(W[36][1][1]-W[14][1][1] for W in (pose(P,V[f]) for f in range(A['frames'])))
    A,V=load_anim(os.path.join(d,'0003.bin'))
    low=[i for i,(p,b,w) in enumerate(v for m in meshes for v in m['v']) if p[1]<0.03]
    feet=[min(sk[i][1] for i in low) for sk in (skin(meshes,P,pose(P,V[f])) for f in range(0,A['frames'],6))]
    print('Clap: closest wrists %.3f (T-pose %.3f)'%(clap,dist(P[33],P[36])))
    print('Wave: right wrist up to %.3f above the neck'%wave)
    print('Stand 0: lowest foot vertex between %.4f and %.4f'%(min(feet),max(feet)))
if __name__=='__main__':
    assert all(depth(b)>=depth(b-1) for b in range(1,71)),'depth rule'
    meshes=load_body(sys.argv[1]); P=positions(meshes)
    if len(sys.argv)==2:
        for b in range(71):
            print('%2d %-13s parent %3s  depth %d  pos %s'%(b,NAMES.get(b,''),PARENT.get(b,'-'),depth(b),
                                                        ' '.join('%7.4f'%x for x in P[b])))
    elif sys.argv[2]=='--score': score(meshes,P,sys.argv[3])
    else:
        A,V=load_anim(sys.argv[2]); frames=[int(x) for x in sys.argv[4].split(',')]
        tris=[]; base=0
        for m in meshes: tris+=[[base+j for j in t] for t in m['tris']]; base+=len(m['v'])
        polys=[skin(meshes,P,pose(P,V[f])) for f in frames]
        for ax,name in (((0,1,2),'front'),((2,1,0),'side')):
            PZ.draw(polys,tris,'%s_%s.png'%(sys.argv[3],name),cell=220,cols=len(frames),ax=ax)
