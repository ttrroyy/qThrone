"""St Patrick pixel studies with a living leprechaun, clover, gold and muted rainbow."""
from pathlib import Path
from PIL import Image, ImageDraw
import math

ROOT=Path(__file__).parent
SIZES={'wide':136,'compact':92,'narrow':48}
BG='#202020'

def ease(v):
    v=max(0,min(1,v));return v*v*(3-2*v)

def eyes(t):
    t=t%24;closed=0
    for a,hold in [(2.2,.18),(7.1,1.2),(12.4,.2),(18.3,1.7),(22.2,.15)]:
        closed=max(closed,ease((t-a)/.2)*(1-ease((t-a-.2-hold)/.3)))
    keys=[(0,0),(3,0),(3.7,-1),(5.4,-1),(6,0),(11,0),(11.7,1),(14.3,1),(15,0),(24,0)]
    gaze=0
    for (a,x),(b,y) in zip(keys,keys[1:]):
        if a<=t<=b:gaze=round(x+(y-x)*ease((t-a)/(b-a)));break
    return 1-closed,gaze

def clover(im,x,y,size,t,seed=0):
    d=ImageDraw.Draw(im);wind=round(math.sin(t*math.tau/12+seed)*.7)
    d.line([(x,y),(x+wind,y-size*2)],fill='#3e6446')
    cx=x+wind;cy=y-size*2
    # Three heart-shaped leaflets, with subdued green veins.
    for dx,dy,turn in [(-1,-1,-.8),(1,-1,.8),(0,-2,0)]:
        xx=cx+round(dx*size);yy=cy+round(dy*size)
        points=[(-1,1),(-2,0),(-2,-1),(-1,-2),(0,-1),(1,-2),(2,-1),(2,0),(1,1),(0,2)]
        color=['#426e49','#578b53','#385f43'][seed%3]
        pts=[]
        scale=size*.6
        for a,b in points:
            pts.append((round(xx+scale*(a*math.cos(turn)-b*math.sin(turn))),round(yy+scale*(a*math.sin(turn)+b*math.cos(turn)))))
        d.polygon(pts,fill='#294a36');d.polygon([(px,py-1) for px,py in pts],fill=color)
        d.line((xx,yy,xx,yy+1),fill='#7a9b60')

def hero(im,x,t):
    d=ImageDraw.Draw(im)
    def poly(p,c):d.polygon([(x+a,b) for a,b in p],fill=c)
    def rect(a,b,w,h,c):d.rectangle((x+a,b,x+a+w-1,b+h-1),fill=c)
    outline='#303c33'
    # Curled tail and folded wings give the creature an unmistakable dragon silhouette.
    poly([(6,36),(12,39),(19,39),(21,36),(19,33),(21,34),(24,38),(22,43),(14,44),(7,42)],outline)
    poly([(9,38),(15,41),(20,41),(22,39),(21,37),(21,40),(18,42),(12,42)],'#4e8454')
    poly([(-8,33),(-15,28),(-18,31),(-18,39),(-14,37),(-12,41),(-7,40)],outline)
    poly([(-10,34),(-15,30),(-16,32),(-16,37),(-13,35),(-11,39),(-8,38)],'#607d50')
    d.line((x-15,31,x-12,37),fill='#375943')
    # Rounded body, scaled olive belly and clawed feet; no human clothes or beard.
    poly([(-8,32),(-11,37),(-10,42),(-6,44),(8,44),(12,41),(11,35),(7,31)],outline)
    poly([(-7,33),(-9,38),(-7,42),(6,43),(10,40),(9,35),(6,33)],'#4e8655')
    poly([(-1,33),(-4,36),(-4,41),(-1,43),(5,42),(7,39),(5,35)],'#a2ac68')
    for yy in [36,39,42]:d.line((x-2,yy,x+4,yy),fill='#7f925b')
    poly([(-10,41),(-13,43),(-11,45),(-4,45),(-3,43)],'#436d49')
    poly([(7,41),(13,42),(14,45),(5,45),(4,43)],'#436d49')
    rect(-11,44,1,1,'#c7c397');rect(-8,44,1,1,'#c7c397');rect(10,44,1,1,'#c7c397');rect(12,44,1,1,'#c7c397')
    # Small horns and ears remain visible below the hat brim.
    poly([(-10,23),(-14,20),(-14,25),(-10,28)],'#446d47')
    poly([(9,23),(13,21),(13,26),(9,28)],'#446d47')
    poly([(-11,24),(-12,18),(-9,21),(-8,24)],'#b5b184')
    poly([(9,24),(12,18),(12,23),(11,26)],'#b5b184')
    poly([(-8,21),(-11,24),(-10,29),(-6,33),(1,35),(8,33),(11,29),(10,24),(7,21)],outline)
    poly([(-7,22),(-9,25),(-8,29),(-5,32),(1,33),(7,31),(9,28),(8,24),(5,22)],'#5b955b')
    rect(-7,24,2,5,'#78a767');rect(6,25,2,5,'#447b4d')
    opening,gaze=eyes(t)
    d.line((x-7,24,x-3,25),fill='#385d40');d.line((x+2,25,x+6,24),fill='#385d40')
    if opening<.2:
        rect(-7,27,4,1,'#2d4136');rect(2,27,4,1,'#2d4136')
    else:
        h=2 if opening>.65 else 1
        rect(-6+gaze,26,2,h,'#d0c77e');rect(3+gaze,26,2,h,'#d0c77e')
        rect(-5+gaze,26,1,h,'#283f34');rect(4+gaze,26,1,h,'#283f34')
    # Long blunt muzzle, nostrils and a restrained changing smile.
    poly([(0,28),(7,27),(14,28),(17,30),(16,33),(11,35),(4,34),(0,32)],'#385d40')
    poly([(2,29),(8,28),(13,29),(15,30),(15,32),(10,33),(4,32)],'#78a568')
    rect(11,29,1,1,'#355c40');rect(14,30,1,1,'#355c40')
    d.line([(x+4,32),(x+10,33),(x+14,32 if 15<t<17 else 33)],fill='#35533b')
    rect(7,33,1,1,'#c5c591')
    # Tall hat with a band and plain brass buckle.
    poly([(-10,5),(-8,3),(8,3),(11,6),(9,18),(15,19),(15,22),(-15,22),(-15,19),(-10,18)],outline)
    poly([(-8,5),(7,5),(9,7),(7,18),(-8,18)],'#37674a')
    rect(-7,6,3,10,'#4e8053');rect(5,7,2,9,'#2d553e')
    rect(-9,15,18,4,'#323d33');rect(-14,19,28,2,'#4b7950')
    rect(-2,14,6,5,'#c4a45a');rect(-1,15,4,3,'#343d33');rect(1,15,1,3,'#c4a45a')
    # A tiny shamrock tucked into the left side of the hat.
    for a,b in [(-8,10),(-10,11),(-6,11)]:rect(a,b,2,2,'#6e9456')
    rect(-8,12,1,2,'#8ba26a')

def goldpot(im,x,t):
    d=ImageDraw.Draw(im)
    d.polygon([(x-9,31),(x-12,34),(x-11,40),(x-7,44),(x+7,44),(x+11,40),(x+12,34),(x+8,31)],fill='#2a3531')
    d.polygon([(x-8,33),(x-9,36),(x-7,41),(x-3,43),(x+7,42),(x+9,38),(x+8,33)],fill='#435348')
    d.line((x-7,35,x-6,39,x-3,41),fill='#60705a')
    for i,(xx,yy) in enumerate([(-7,31),(-3,29),(1,30),(5,29),(8,31),(-1,32),(4,32)]):
        d.ellipse((x+xx-2,yy-1,x+xx+2,yy+1),fill='#9b8147')
        d.line((x+xx-1,yy-1,x+xx+1,yy-1),fill='#d9bd6b')
    d.line((x-9,33,x+9,33),fill='#28352f')
    for xx,yy in [(14,43),(18,42),(15,40)]:
        d.line((x+xx-2,yy,x+xx+1,yy),fill='#b59a57')
    # Only one coin catches light at a time, in a gentle repeating cycle.
    k=int(t/3)%4
    if k<3:d.point((x+[-3,5,1][k],[28,28,29][k]),fill='#ede0a3')

def rainbow(im):
    d=ImageDraw.Draw(im)
    # A small muted arc behind the greenery, no full-screen rainbow overlay.
    for inset,color in enumerate(['#795b4f','#94804f','#617b51','#4c7370']):
        d.arc((5+inset,7+inset,59-inset,77-inset),180,360,fill=color,width=1)

def frame(kind,t):
    t=t%24;w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    if kind=='wide':rainbow(im)
    d.polygon([(0,43),(12,40),(28,41),(w//2,39),(w-19,41),(w-1,42),(w-1,45),(0,45)],fill='#2c4535')
    for i in range(w//8):
        x=i*9+2;d.line((x,44,x+4,44-i%2),fill='#425c3e')
    if kind=='wide':
        for i,(x,y,s) in enumerate([(5,43,3),(16,39,3),(47,43,3),(94,42,3),(107,43,4),(121,41,3),(132,44,3)]):clover(im,x,y,s,t,i)
        goldpot(im,28,t);hero(im,73,t)
        for i,(x,y,s) in enumerate([(4,45,2),(48,45,2),(95,45,2),(115,44,3),(133,45,2)]):clover(im,x,y,s,t,i+10)
    elif kind=='compact':
        for i,(x,y,s) in enumerate([(4,43,3),(33,44,3),(74,42,3),(86,44,3)]):clover(im,x,y,s,t,i)
        goldpot(im,17,t);hero(im,53,t)
        clover(im,35,45,2,t,11);clover(im,80,45,3,t,14)
    else:
        clover(im,5,43,3,t,2);clover(im,43,43,3,t,4)
        hero(im,25,t)
        clover(im,7,45,2,t,11);clover(im,45,45,2,t,12)
    return im

def main():
    board=Image.new('RGB',(680,710),BG);d=ImageDraw.Draw(board)
    for j,(kind,w) in enumerate(SIZES.items()):
        frames=[]
        for i in range(240):
            art=frame(kind,i/10);out=Image.new('RGB',art.size,BG);out.paste(art,mask=art.getchannel('A'))
            frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'patrick-{kind}-v2.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
        art=frame(kind,2.5);art.save(ROOT/f'patrick-{kind}-v2.png')
        d.text((20,15+j*230),kind.upper(),fill='#c7c9b7')
        art=art.resize((w*4,184),Image.Resampling.NEAREST);board.paste(art,(20,42+j*230),art)
        print(kind,(ROOT/f'patrick-{kind}-v2.gif').stat().st_size)
        assert frame(kind,0).tobytes()==frame(kind,24).tobytes()
    board.save(ROOT/'contact-sheet-v2.png')

if __name__=='__main__':main()
