"""Four non-holiday seasonal scenes, in the approved decoration proportions."""
from PIL import Image,ImageDraw
import math,random

SIZES={'wide':136,'compact':92}

class ScaledDraw:
    def __init__(self,draw,cx,baseline,scale,dy=0):self.draw=draw;self.cx=cx;self.baseline=baseline;self.scale=scale;self.dy=dy
    def __getattr__(self,name):
        method=getattr(self.draw,name)
        def call(xy,*args,**kwargs):
            def point(x,y):return (self.cx+(x-self.cx)*self.scale,self.baseline+(y-self.baseline)*self.scale+self.dy)
            if isinstance(xy[0],(tuple,list)):coords=[point(x,y) for x,y in xy]
            else:
                coords=[]
                for i in range(0,len(xy),2):coords.extend(point(xy[i],xy[i+1]))
                coords=tuple(coords)
            if 'width' in kwargs:kwargs['width']*=self.scale
            if 'radius' in kwargs:kwargs['radius']*=self.scale
            return method(coords,*args,**kwargs)
        return call

def ease(v):
    v=max(0,min(1,v));return v*v*(3-2*v)

def eyes(d,x,y,t,color='#252c27',spacing=3.5):
    q=t%24;closed=0
    for start,hold in [(3,.15),(8,1.1),(15,.18),(20,.65)]:
        closed=max(closed,ease((q-start)/.22)*(1-ease((q-start-.22-hold)/.3)))
    h=max(.25,2.1*(1-closed));gaze=.55*math.sin(t*math.tau/24)
    for side in [-1,1]:
        xx=x+side*spacing+gaze
        d.ellipse((xx-.65,y-h/2,xx+.65,y+h/2),fill=color)
        if h>1.5:d.ellipse((xx-.25,y-.55,xx+.02,y-.28),fill='#ded7b6')

def flower(d,x,y,t,seed=0,r=2,color='#cbb8c5'):
    sway=.65*math.sin(t*math.tau/12+seed);cx=x+sway
    d.line([(x,45),(x-.4,y+3),(cx,y)],fill='#52734c',width=1)
    d.ellipse((x-3,y+4,x,y+6),fill='#648751')
    for j in range(5):
        a=j*math.tau/5;xx=cx+math.cos(a)*r*.65;yy=y+math.sin(a)*r*.65
        d.ellipse((xx-r*.65,yy-r*.65,xx+r*.65,yy+r*.65),fill=color)
    d.ellipse((cx-.65,y-.65,cx+.65,y+.65),fill='#d6b968')

def grass(d,x,y,t,seed=0,size=1):
    sway=.7*math.sin(t*math.tau/12+seed)
    for dx,dy in [(-5,5),(-3,10),(0,13),(3,9),(5,5)]:
        d.line([(x,y),(x+dx*size*.5+sway*.4,y-dy*size*.65),(x+dx*size+sway,y-dy*size)],fill=['#49674a','#668552','#3b5e46'][seed%3])

def mushroom(d,x,y,r=3,color='#a65e41'):
    d.rectangle((x-.6,y-3,x+.7,y+1),fill='#b4a17d')
    d.ellipse((x-r,y-r-3,x+r,y-1),fill='#493a32')
    d.ellipse((x-r+.3,y-r-2.7,x+r-.3,y-1.5),fill=color)
    d.line((x-r+1,y-2,x+r-1,y-2),fill='#c4a580')
    d.ellipse((x-r*.5,y-r-1.7,x-r*.5+.6,y-r-1.1),fill='#d3b58e')

def leaf(d,x,y,r,color,angle=0):
    # Fine toothed, pointed leaves rather than polygonal tiles.
    p=[]
    for j in range(32):
        q=j*math.tau/32
        yy=-r*math.cos(q)
        xx=r*.52*math.sin(q)*(1+.065*math.cos(q*16))
        p.append((x+xx*math.cos(angle)-yy*math.sin(angle),y+xx*math.sin(angle)+yy*math.cos(angle)))
    d.polygon(p,fill=color)
    ca,sa=math.cos(angle),math.sin(angle)
    d.line((x+r*.82*sa,y-r*.82*ca,x-r*.82*sa,y+r*.82*ca),fill='#9aa46b' if color in ['#6f8c50','#91a85d','#56774b'] else '#b58f56')
    for sign in [-1,1]:
        for yy in [-.35,.15,.48]:
            a=yy*r;b=a-r*.20
            xx=sign*r*.30
            d.line((x-a*sa,y+a*ca,x+xx*ca-b*sa,y+xx*sa+b*ca),fill='#819459' if color in ['#6f8c50','#91a85d','#56774b'] else '#9b7748')

def fern(d,x,y,t,seed=0):
    sway=.6*math.sin(t*math.tau/12+seed)
    for side,h in [(-1,13),(0,16),(1,12)]:
        endx=x+side*8+sway
        d.line((x,y,endx,y-h),fill='#58774c')
        for j in range(2,7):
            f=j/7;cx=x+(endx-x)*f;cy=y-h*f;span=2.4*(1-f)+.4
            for sign in [-1,1]:
                d.line((cx,cy,cx+sign*span,cy-1.8),fill='#698b52' if j%2 else '#496e49')


def bush(d,x,y,t,seed=0,berry='#a3564b',autumn=False,height=15):
    sway=.5*math.sin(t*math.tau/12+seed)
    for side,h in [(-1,height*.75),(0,height),(1,height*.83)]:
        tip=x+side*5+sway;top=y-h
        d.line([(x,y),(x+side*2,y-h*.45),(tip,top)],fill='#655e40' if autumn else '#496348')
        for j in range(2,6):
            f=j/6;px=x+(tip-x)*f;py=y-h*f
            for sign in [-1,1]:
                col=['#607349','#7a7c48','#566943'][j%3] if autumn else ['#446b49','#5f8050','#385c43'][j%3]
                d.ellipse((px+sign*1.8-1.5,py-1.3,px+sign*1.8+1.5,py+.6),fill=col)
                d.line((px+sign*.4,py,px+sign*2.4,py-.45),fill='#8a8a58' if autumn else '#7c925f')
        for j,(dx,dy) in enumerate([(-1,-.7),(1.1,-.6),(0,1)]):
            bx=tip+dx;by=top+dy
            d.ellipse((bx-1,by-1,bx+1,by+1),fill=berry)
            d.ellipse((bx-.55,by-.65,bx-.05,by-.15),fill='#d29d7b' if berry!='#536a81' else '#9aa8b4')

def meadow(d,w,t,season):
    for j,x in enumerate(range(3,w,9)):
        size=.35+(j%4)*.12
        grass(d,x,45,t,j,size)
        if j%3==0:
            for side in [-1,1]:
                d.ellipse((x+side*2-2,41-j%3,x+side*2+2,43-j%3),fill='#586f47')
    if season=='spring':
        for j,x in enumerate([15,38,w-32,w-9]):bush(d,x,44,t,j,berry='#a1a782',height=8+j%2*3)
        for j,x in enumerate(range(10,w,17)):
            flower(d,x,31+j%3*3,t,j,1.2,['#b1bda2','#cbb16b','#b4a7bc'][j%3])
    elif season=='summer':
        for j,x in enumerate([5,37 if w>100 else 29,w-12,w-29]):
            bush(d,x,44,t,j,berry='#9b5147' if j%2 else '#536a81',height=11+j%3*3)
    elif season=='autumn':
        for j,x in enumerate([8,37,w-12,w-30]):bush(d,x,44,t,j,berry='#875346',autumn=True,height=9+j%3*3)
        for j,x in enumerate(range(8,w,13)):
            d.ellipse((x-1.2,42,x+1.2,44),fill='#a08a57')
            d.line((x-1.2,42,x+1.2,42),fill='#6c5941',width=1)

def ground(d,w,color,highlight):
    d.polygon([(0,44),(8,41),(w*.33,42),(w*.64,40),(w-8,42),(w-1,44),(w-1,45),(0,45)],fill=color)
    d.line((2,45,w-3,45),fill=highlight)

def birch(d,x,t):
    d.line([(x-1,44),(x-1,14),(x-4,5)],fill='#444f43',width=7)
    d.line([(x-1,43),(x-1,14),(x-4,5)],fill='#d4d4b9',width=5)
    d.line((x+1,42,x+1,17),fill='#a7b7a4')
    for yy,side in [(16,-1),(20,1),(32,-1),(37,1),(41,-1)]:
        d.line((x+side*2,yy,x+side*.2,yy+.5),fill='#5a655a')
    for side in [-1,1]:
        d.line([(x,20),(x+side*9,12),(x+side*14,6)],fill='#bcc8ae',width=2)
        d.line((x+side*9,12,x+side*18,10),fill='#9aa88b')
    clusters=[(-16,10),(-11,6),(-6,4),(0,5),(6,5),(12,8),(15,13),(-12,16),(-6,12),(5,13)]
    for i,(dx,yy) in enumerate(clusters):
        d.line((x+dx*.5,yy-1,x+dx,yy+1),fill='#7d9270')
        for j,(lx,ly) in enumerate([(-2,0),(0,-1),(2,.5),(-.5,2.7)]):
            wind=.45*math.sin(t*math.tau/12+i+j*.3)
            leaf(d,x+dx+lx+wind,yy+ly,1.9+(j%2)*.3,['#6f8c50','#91a85d','#56774b'][(i+j)%3],(j-1.5)*.35)
    # A woven hive hangs securely from the birch trunk, not a face.
    d.line((x-1,17,x+6,19),fill='#8b754c',width=1)
    hx=x+5
    d.ellipse((hx-4,19,hx+4,31),fill='#7c6743')
    for j in range(5):
        yy=21+j*2;span=3.1 if j in (1,2,3) else 2.3
        d.ellipse((hx-span,yy-1,hx+span,yy+1.2),fill=['#b29b65','#a38a56'][j%2])
    d.ellipse((hx-1,26,hx+1,28.5),fill='#4b4430')


class BeeFacing:
    def __init__(self,draw,x,facing):self.draw=draw;self.x=x;self.facing=facing
    def ellipse(self,box,**kw):
        a,y,b,yy=box
        a=self.x+(a-self.x)*self.facing;b=self.x+(b-self.x)*self.facing
        self.draw.ellipse((min(a,b),y,max(a,b),yy),**kw)
    def line(self,points,**kw):
        a,y,b,yy=points
        self.draw.line((self.x+(a-self.x)*self.facing,y,self.x+(b-self.x)*self.facing,yy),**kw)

def route_facing(keys,q):
    # Turn around at the slow end of each leg, including across the loop seam.
    signs=[]
    for a,b in zip(keys,keys[1:]):
        signs.append(1 if b[1]>a[1] else -1 if b[1]<a[1] else 0)
    for i in range(len(signs)):
        if signs[i]==0:
            signs[i]=next(signs[(i-j)%len(signs)] for j in range(1,len(signs)) if signs[(i-j)%len(signs)])
    for i,(a,b) in enumerate(zip(keys,keys[1:])):
        if a[0]<=q<=b[0]:
            current=signs[i];previous=signs[(i-1)%len(signs)];following=signs[(i+1)%len(signs)]
            if q<a[0]+.18:return previous+(current-previous)*ease((q-a[0]+.18)/.36)
            if q>b[0]-.18:return current+(following-current)*ease((q-b[0]+.18)/.36)
            return current
    return signs[0]

def bee_at(d,xx,yy,t,phase=0,resting=False,facing=1):
    d=BeeFacing(d,xx,facing)
    wing=.5+.5*math.sin(t*math.tau*4+phase)
    if resting:
        d.ellipse((xx-1.2,yy-1.3,xx+.8,yy-.3),fill='#abc3bd')
        for dx in [-.8,.7]:d.line((xx+dx,yy+.5,xx+dx+.4,yy+1.4),fill='#746444')
    else:
        for side in [-1,1]:d.ellipse((xx-1+side*.7,yy-2-wing,xx+1+side*.7,yy-.2),fill='#abc3bd')
    d.ellipse((xx-2,yy-1,xx+2,yy+1.2),fill='#c9ad5e')
    for dx in [-.8,.6]:d.line((xx+dx,yy-.7,xx+dx,yy+.8),fill='#544a32')
    d.ellipse((xx+1.2,yy-.6,xx+2.4,yy+.7),fill='#464839')

def route(keys,q):
    for (a,x,y,s),(b,xx,yy,ss) in zip(keys,keys[1:]):
        if a<=q<=b:
            u=ease((q-a)/(b-a))
            return x+(xx-x)*u,y+(yy-y)*u,s+(ss-s)*u
    return keys[0][1:]

def spring_bees(d,w,c,t):
    # The nearest bee enters the same drawn entrance at (c+5,27.2).
    hx=c+5;q=t%24
    fi=min((w-8-4)//6,(c+17-4)//6);fx=4+fi*6;fy=36+(fi%3)*2
    home=(c+12,23)
    keys=[(0,*home,1),(2,c+15,20,1),(4,hx+3,26,1),(5,hx,27.2,.12),
          (8,hx,27.2,.12),(9,hx+3,26,1),(12,fx,fy-1.7,1),(15,fx,fy-1.7,1),
          (18,c+22,22,1),(21,c+16,19,1),(24,*home,1)]
    if not 5<=q<8:
        xx,yy,scale=route(keys,q)
        bee_at(ScaledDraw(d,xx,yy,scale),xx,yy,t,resting=12<=q<=15,facing=route_facing(keys,q))
    for i,(sx,sy,idx) in enumerate([(16,28,3),(w-15,26,max(1,(w-18-4)//6))]):
        q=(t+i*7)%24;fx=4+idx*6;fy=36+(idx%3)*2
        keys=[(0,sx,sy,1),(3,sx+4,sy-3,1),(6,fx,fy-1.7,1),(9,fx,fy-1.7,1),
              (12,sx-2,sy-2,1),(17,sx+3,sy-4,1),(21,sx-2,sy,1),(24,sx,sy,1)]
        xx,yy,scale=route(keys,q)
        bee_at(d,xx,yy,t,i*1.7,resting=6<=q<=9,facing=route_facing(keys,q))


def spring(d,w,t):
    c=77 if w>100 else 51
    for x in [6,19,w-9,w-24]:fern(d,x,45,t,int(x))
    meadow(d,w,t,'spring')
    birch(d,c,t)
    ground(d,w,'#415b3e','#6c8150')
    for i,x in enumerate(range(4,w-3,6)):
        if abs(x-c)<5:continue
        flower(d,x,36+(i%3)*2,t,i,1.8,['#d0b7c5','#c4cbb3','#d2bd78'][i%3])
    spring_bees(d,w,c,t)
    for x,y in [(c-10,43),(c+11,44)]:d.ellipse((x-3,y-1,x+3,y+1),fill='#70816b')

def frog(d,x,t):
    d.ellipse((x-15,32,x+14,44),fill='#344a39')
    d.ellipse((x-13,33,x+12,43),fill='#5a8250')
    for side in [-1,1]:
        d.ellipse((x+side*13-4,38,x+side*13+4,44),fill='#6a9259')
        d.ellipse((x+side*7-5,19,x+side*7+5,29),fill='#344a39')
        d.ellipse((x+side*7-4,20,x+side*7+4,28),fill='#7a9a5d')
    d.ellipse((x-14,24,x+14,38),fill='#648e54')
    d.ellipse((x-10,31,x+9,42),fill='#a6af74')
    eyes(d,x,24,t,color='#263d2c',spacing=7)
    d.line([(x-7,32),(x,33),(x+7,32)],fill='#3a5b3b')
    for side in [-1,1]:
        d.line([(x+side*8,34),(x+side*9,41),(x+side*5,43)],fill='#4a7147',width=2)
        for dx in [-1,1]:d.line((x+side*5,43,x+side*5+dx*2,44),fill='#6e965c')

def hedgehog(d,x,t):
    bodyx=x-5
    for i in range(24):
        a=i*math.tau/24
        xx=bodyx+math.cos(a)*13;yy=29+math.sin(a)*12
        tipx=bodyx+math.cos(a)*(15+i%3*.4);tipy=29+math.sin(a)*(14+i%2*.5)
        d.polygon([(xx-1.3,yy),(tipx,tipy),(xx+1.3,yy+1)],fill=['#5b5040','#7d7050','#8d805a'][i%3])
    d.ellipse((bodyx-13,17,bodyx+13,42),fill='#6e6149')
    rng=random.Random(19)
    for i in range(27):
        a=rng.random()*math.tau;rr=math.sqrt(rng.random())
        xx=bodyx+math.cos(a)*11*rr;yy=29+math.sin(a)*10*rr
        lean=(xx-bodyx)*.09
        d.line((xx,yy,xx+lean,yy+1.1+rng.random()*.7),fill=['#a1936a','#514939','#8f7e58'][i%3])
    d.ellipse((x+1,25,x+11,40),fill='#b7a27b')
    d.polygon([(x+5,30),(x+11,30),(x+18,35),(x+15,38),(x+7,39),(x+4,36)],fill='#ccb38a')
    d.ellipse((x+2,24,x+7,29),fill='#9f8b69')
    d.ellipse((x+3,25,x+6,28),fill='#c2a889')
    eyes(d,x+9,31,t,color='#332f28',spacing=0)
    d.ellipse((x+16,34,x+18.5,36),fill='#3e362e')
    for dx in [-11,4,11]:d.ellipse((x+dx-2,41,x+dx+3,44),fill='#65503a')
    for j,dx in enumerate([-22,-17,20]):
        yy=42-j%2;d.ellipse((x+dx-1.7,yy-2,x+dx+1.7,yy+1),fill='#a35d4c')
        d.line((x+dx,yy-2,x+dx+1,yy-3),fill='#55704b')
        d.point((x+dx-.5,yy-.3),fill='#d1ac77')

def summer(d,w,t):
    c=87 if w>100 else 58
    for i,x in enumerate([4,14,w-7,w-18]):grass(d,x,45,t,i,1.35)
    for x in [5,w-10]:
        d.line((x,43,x+2,16),fill='#587b4b')
        d.ellipse((x+1,11,x+3,21),fill='#866c43')
    for j,x in enumerate([6,w-7,w-27]):fern(d,x,45,t,j)
    ground(d,w,'#445b3c','#71834e')
    right=59 if w>100 else 31
    d.ellipse((1,37,right,45),fill='#354f50')
    for i in range(3):
        xx=8+i*10+math.sin(t*math.tau/6+i)*1.4
        d.line((xx,40+i%2,xx+8,40+i%2),fill='#77958b')
    for i,x in enumerate([13,right-10]):
        d.ellipse((x-5,38+i,x+5,42+i),fill='#567c50')
        d.line((x,40+i,x+4,38+i),fill='#304f43')
        flower(d,x,37+i,t,i,1.6,'#d5b3be')
    d.ellipse((c-18,40,c+20,45),fill='#6f7860')
    meadow(d,w,t,'summer')
    hedgehog(ScaledDraw(d,c,44,.62),c,t)
    for i,x in enumerate([w-5,w-19 if w>100 else w-12,4]):flower(d,x,29+i*3,t,i,2.4,'#d1b05d')
    xx=35 if w>100 else 20;yy=16+math.sin(t*math.tau/8)
    xx+=4*math.sin(t*math.tau/12)
    for side in [-1,1]:
        d.ellipse((xx-4,yy+side*1.4-1,xx+4,yy+side*1.4+1),fill='#97bcb1')
    d.line((xx,yy-2,xx,yy+3),fill='#628e8b')

def fox(d,x,t):
    d.ellipse((x-24,32,x-4,44),fill='#754633')
    d.ellipse((x-23,33,x-3,43),fill='#b77843')
    d.ellipse((x-23,33,x-15,42),fill='#cec0a1')
    d.polygon([(x-8,27),(x+7,27),(x+12,42),(x+6,44),(x-7,44),(x-11,41)],fill='#8f5737')
    d.polygon([(x-3,28),(x+4,28),(x+6,40),(x,43),(x-4,39)],fill='#c9b48e')
    d.polygon([(x-12,25),(x-11,12),(x-3,18),(x+4,18),(x+12,12),(x+12,25),(x+6,32),(x,34),(x-7,31)],fill='#694332')
    d.polygon([(x-10,24),(x-9,15),(x-3,20),(x+4,20),(x+10,15),(x+10,25),(x+5,30),(x,32),(x-6,29)],fill='#c58a4c')
    for side in [-1,1]:
        d.polygon([(x+side*10,24),(x+side*6,27),(x,29),(x+side*4,32)],fill='#dbcaac')
        d.line((x+side*8,17,x+side*8,20),fill='#8c6250')
    eyes(d,x,24,t,spacing=5)
    d.ellipse((x-1.1,28,x+1.1,29.5),fill='#453c32')
    for side in [-1,1]:d.ellipse((x+side*5-3,42,x+side*5+3,44),fill='#654331')

def autumn(d,w,t):
    c=86 if w>100 else 60;tree=20 if w>100 else 12
    d.line([(tree,44),(tree+1,17),(tree-4,7)],fill='#65513b',width=5)
    for side in [-1,1]:d.line((tree,25,tree+side*13,13),fill='#6f573c',width=2)
    for i,(dx,y) in enumerate([(-12,12),(-6,6),(1,7),(9,9),(15,16),(-10,21),(-3,18),(6,18)]):
        for j,(lx,ly) in enumerate([(-2,0),(0,-2),(2,.4),(-.8,2.5)]):
            cx=tree+dx+lx+.4*math.sin(t*math.tau/12+i);cy=y+ly
            col=['#9d7945','#a4643e','#7b7847','#b3884e'][(i+j)%4]
            d.ellipse((cx-1.8,cy-2.5,cx+1.8,cy+2.5),fill=col)
            d.line((cx,cy+1.5,cx+.1,cy-1.7),fill='#907445')
    for j,x in enumerate([w-5,36 if w>100 else 29]):fern(d,x,45,t,j)
    ground(d,w,'#514a33','#887247')
    meadow(d,w,t,'autumn')
    fox(ScaledDraw(d,c,44,.66),c,t)
    for i,x in enumerate([5,14,38,w-8,w-19]):mushroom(d,x,43,3+i%2,['#a06440','#b5844c'][i%2])
    for i,x in enumerate(range(3,w,8)):leaf(d,x,44-(i%2),2,['#a37b43','#885d38','#9c663a'][i%3],i*.9)
    for x in [31 if w>100 else 25,w-3]:grass(d,x,44,t,int(x),.7)
    for i in range(4 if w>100 else 2):
        q=(t/24+i*.29)%1;y=q*60-8
        x=(34+i*23 if w>100 else 26+i*30)+2*math.sin(t*math.tau/12+i)
        leaf(d,x,y,1.8,'#b58a4d',t*math.tau/8+i)

def fir(d,x,top,width=13):
    d.rectangle((x-1.6,top+17,x+1.6,44),fill='#675844')
    h=40-top
    d.polygon([(x,top),(x-width*.5,top+h*.4),(x-width*.3,top+h*.4),(x-width*.8,top+h*.7),(x-width*.55,top+h*.7),(x-width,40),(x+width,40),(x+width*.55,top+h*.7),(x+width*.8,top+h*.7),(x+width*.3,top+h*.4),(x+width*.5,top+h*.4)],fill='#263e37')
    d.polygon([(x,top+2),(x-width*.7,top+h*.7),(x-width*.35,top+h*.7),(x-width*.8,38),(x,37)],fill='#34584a')
    for yy,span in [(top+h*.4,width*.45),(top+h*.7,width*.72),(39,width*.85)]:
        d.line((x-span,yy,x+span*.55,yy),fill='#aac3bd',width=1)


def owl(d,x,t):
    d.line((x-13,42,x+15,42),fill='#625341',width=3)
    d.ellipse((x-11,17,x+11,41),fill='#4b5650')
    d.ellipse((x-9,18,x+9,40),fill='#87918a')
    d.polygon([(x-11,22),(x-10,12),(x-5,17),(x+5,17),(x+10,12),(x+11,23)],fill='#68766c')
    for side in [-1,1]:
        d.ellipse((x+side*5-4.8,19,x+side*5+4.8,29),fill='#bac1ac')
        d.ellipse((x+side*8-3,28,x+side*8+3,38),fill='#64796d')
    eyes(d,x,24,t,color='#293d37',spacing=5)
    d.polygon([(x-1.3,27),(x+1.3,27),(x,30)],fill='#b3a266')
    for yy in [32,35,38]:
        for xx in [-3,2]:d.line((x+xx,yy,x+xx+1,yy+1),fill='#c6c8af')
    for side in [-1,1]:d.line((x+side*4,40,x+side*4,43),fill='#aa9970',width=1)

def bullfinch(d,x,t):
    d.line((x-19,42,x+20,42),fill='#6b5542',width=2)
    d.line((x-17,41.1,x-5,41.1),fill='#cad5c9',width=1)
    d.polygon([(x-9,32),(x-18,38),(x-17,42),(x-7,38)],fill='#34434a')
    d.ellipse((x-13,19,x+12,39),fill='#4c5c5f')
    d.ellipse((x-7,23,x+12,39),fill='#b16a5a')
    d.ellipse((x-11,20,x+3,34),fill='#657778')
    d.polygon([(x-11,24),(x+1,25),(x-2,34),(x-9,33)],fill='#3e4c50')
    d.line((x-9,25,x,27),fill='#b0b8a9',width=1.4)
    d.ellipse((x+1,12,x+14,25),fill='#303f41')
    d.ellipse((x+2,19,x+12,29),fill='#ac6656')
    d.polygon([(x+12,19),(x+17,21),(x+12,23)],fill='#55554b')
    eyes(d,x+9,18.5,t,color='#17272a',spacing=0)
    for dx in [-2,5]:
        d.line((x+dx,37,x+dx+.5,41),fill='#8c765a')
        d.line((x+dx-1.2,42,x+dx+1.8,42),fill='#b19b75')

def winter(d,w,t):
    fir(d,19 if w>100 else 12,4,16 if w>100 else 13)
    fir(d,46 if w>100 else 31,12,11)
    fir(d,w-11,7,13)
    ground(d,w,'#a9bdb5','#d4ddd0')
    # Low evergreen lingonberry shrubs; red berries remain visible under the snow.
    positions=[10,35,62,88,116,w-5] if w>100 else [8,31,55,79,w-4]
    for j,x in enumerate(positions):
        bush(d,x,44,t,j,berry='#a45550',height=12+(j%3)*3)
        d.ellipse((x-4,42,x+4,44),fill='#c6d1c3')
        if j%2==0:d.line((x-3,31-j%3,x+2,30.6-j%3),fill='#b9cbbd',width=1)
    anchors=[(33,30),(72,28),(113,32)] if w>100 else [(28,30),(68,29)]
    for i,(bx,perch) in enumerate(anchors):
        q=(t+i*2.2)%8
        if q<1:
            u=ease(q);dx=-2+4*u;hop=-2.8*math.sin(math.pi*u)**2
        elif q<4:dx=2;hop=0
        elif q<5:
            u=ease(q-4);dx=2-4*u;hop=-2.8*math.sin(math.pi*u)**2
        else:dx=-2;hop=0
        d.line([(bx-6,perch+1),(bx,perch),(bx+6,perch+1),(bx+7,43)],fill='#5d5c43',width=1)
        draw=ScaledDraw(d,bx+dx,44,.34,dy=perch-43.32+hop)
        bullfinch(draw,bx+dx,t+i*2)
    for i in range(28 if w>100 else 19):
        x=2+((i*.618)%1)*(w-4)+math.sin(t*math.tau/12+i)*1.5
        y=((i*.414+t/24*(1+i%2))%1)*58-7
        d.ellipse((x-.25,y-.25,x+.25,y+.25),fill='#c7d8cf')
    for x in [12,w-12,w*.5]:d.ellipse((x-5,43,x+5,45),fill='#d0d9cb')


def frame(theme,kind,t):
    w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    {'spring':spring,'summer':summer,'autumn':autumn,'winter':winter}[theme](d,w,t%24)
    return im
