"""Avatar animation block (STRB id 1): header, channel headers, frame data (SPECS.md, section 4.5).

A part is a channel table and its frames. The avatar part's table starts at 0x30. Every table has 216 header slots
(72 bones x 3), 146 bits each; the used ones list the bones backwards (slot t is bone n-1-t, three channels each:
translation, rotation, scale; each a step, min x y z and three widths), the unused ones are zero bits. Then the
frames: per frame, bones 0 to n-1 in order, per bone its channels' animated axes, `width` bits each, LSB first.
If the header's extra-bone count is not zero, a prop part follows at the next byte: u32 frames, u32 bones, then a
table and frames in the same form.

Values are min + q * step * K (K per axis, as meshes); an axis of width 0 holds min. Per bone and frame they are
a translation and a rotation as a quaternion logarithm (axis times HALF the angle, as in skeleton blocks; v and
v + pi * axis are the same rotation, and the data switches between them) in the bone's own rest frame, and a
scale: the bone's local transform is its rest local transform followed by translate(T) rotate(R) scale(S).
"""
import sys,os,struct,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from strb import blocks
STEP=0x36ED4402
SLOTS=216
EMPTY_HEADERS_BITS=3*146   # the avatar part: 213 used slots, 3 empty
K=(2.0,2.0/math.sqrt(1.5),math.sqrt(3.0))
def fbits(v): return struct.unpack('<f',struct.pack('<I',v))[0]
def parse(b):
    h=struct.unpack_from('<IfIIIIIIIIII',b,0)
    frames,fps,bones=h[0],h[1],h[2]
    n=int.from_bytes(b,'little'); val=lambda p,w:(n>>p)&((1<<w)-1)
    pos=0x30*8; ch=[]
    while val(pos,32)==STEP:
        mn=[fbits(val(pos+32+32*j,32)) for j in range(3)]
        w=[val(pos+128+6*j,6) for j in range(3)]
        ch.append(dict(at=pos,step=fbits(val(pos,32)),min=mn,w=w)); pos+=146
    return dict(h=h,frames=frames,fps=fps,bones=bones,channels=ch,data_bit=pos,n=n,val=val)
def _table(val,pos,nb):
    ch=[]
    for _ in range(3*nb):
        assert val(pos,32)==STEP,pos
        ch.append(dict(at=pos,step=fbits(val(pos,32)),min=[fbits(val(pos+32+32*j,32)) for j in range(3)],
                       w=[val(pos+128+6*j,6) for j in range(3)])); pos+=146
    return ch
def parts(b):
    """[avatar part, prop part (if any)]: dict(bones, channels, data_bit, end_bit)."""
    A=parse(b); h=A['h']; F=A['frames']; val=A['val']; out=[]
    pos=0x30*8; nb=A['bones']
    for extra in (False,True):
        if extra:
            if not h[3]: break
            at=(out[-1]['end_bit']+7)//8
            assert struct.unpack_from('<II',b,at)==(F,h[3]),'prop part header'
            pos=(at+8)*8; nb=h[3]
        ch=_table(val,pos,nb); data=pos+SLOTS*146
        assert val(pos+len(ch)*146,(SLOTS-len(ch))*146)==0,'unused slots'
        P=sum(sum(c['w']) for c in ch)
        out.append(dict(bones=nb,channels=ch,data_bit=data,end_bit=data+P*F))
    return A,out
def frames(A,part=None):
    """Raw quantised values: frames[f][bone][kind][axis] (kind 0 translation, 1 rotation, 2 scale), None where the
    axis isn't animated (its value is the channel's min). Without `part`, the avatar part."""
    if part is None: part=dict(channels=A['channels'],data_bit=A['data_bit']+EMPTY_HEADERS_BITS)
    ch=part['channels']; nt=len(ch)//3; val=A['val']; p=part['data_bit']; out=[]
    for f in range(A['frames']):
        frame=[]
        for bone in range(nt):
            t=nt-1-bone; kinds=[]
            for k in range(3):
                axes=[]
                for a in range(3):
                    w=ch[3*t+k]['w'][a]
                    axes.append(val(p,w) if w else None); p+=w
                kinds.append(axes)
            frame.append(kinds)
        out.append(frame)
    return out
def channel(A,bone,kind,part=None):
    """The channel header of a bone's translation (0), rotation (1) or scale (2)."""
    ch=A['channels'] if part is None else part['channels']
    return ch[3*(len(ch)//3-1-bone)+kind]
def values(A,part):
    """values[f][bone] = (T, R, S) as floats: min + q * step * K."""
    Fr=frames(A,part); nb=len(part['channels'])//3; out=[]
    for f in range(A['frames']):
        row=[]
        for bone in range(nb):
            row.append(tuple([c['min'][a]+(Fr[f][bone][k][a] or 0)*c['step']*K[a] for a in range(3)]
                             for k,c in ((k,channel(A,bone,k,part)) for k in range(3))))
        out.append(row)
    return out
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); b=[d[a:a+l] for i,a,l,f in blocks(d) if i==1][0]
    A,P=parts(b)
    print('frames',A['frames'],'fps %.2f'%A['fps'],'section offsets',A['h'][6:10])
    for i,part in enumerate(P):
        per=sum(sum(c['w']) for c in part['channels'])
        print('%s part: %d bones, bits/frame %d, frames from bit %d to %d (byte %.2f)'%(
            ('avatar','prop')[i],part['bones'],per,part['data_bit'],part['end_bit'],part['end_bit']/8))
        V=values(A,part)
        for bone in range(part['bones']):
            c=channel(A,bone,1,part)
            if any(c['w']):
                print('  bone %2d rotation (quaternion log), frames 0 1 2: %s'%(bone,'  '.join(
                    ' '.join('%6.3f'%x for x in V[f][bone][1]) for f in range(3))))
