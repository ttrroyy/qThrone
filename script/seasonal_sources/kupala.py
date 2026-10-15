"""Kupala woodland studies: living stump, fire, wreath, lake, fern and a shy owl."""
from pathlib import Path
from PIL import Image, ImageDraw
import math

ROOT=Path(__file__).parent
SIZES={'wide':136,'compact':92,'narrow':48}
BG='#202020'

def ease(v):
    v=max(0,min(1,v));return v*v*(3-2*v)


def eye_state(t,owl=False):
    q=(t+(4.3 if owl else 0))%24
    closed=0
    for start,hold in [(2.4,.16),(7.2,1.5),(13.5,.24),(18.2,2.1),(22.5,.18)]:
        rise=ease((q-start)/.22)
        fall=1-ease((q-start-.22-hold)/.30)
        closed=max(closed,rise*fall)
    keys=[(0,0),(3,0),(3.6,-1),(5.6,-1),(6.2,0),(11.2,0),(11.8,1),(14.2,1),(14.8,0),(20.8,0),(21.4,-1),(22,-1),(22.7,0),(24,0)]
    gaze=0
    for (a,x),(b,y) in zip(keys,keys[1:]):
        if a<=q<=b:
            gaze=x+(y-x)*ease((q-a)/(b-a));break
    return 1-closed,round(gaze*(-1 if owl else 1))


def fern(im,x,y,side,t,size=1):
    d=ImageDraw.Draw(im);wind=round(math.sin(t*math.tau/12+x)*1)
    for spread,height in [(0,15),(8,13),(13,8)]:
        xx=x+round(side*spread*size)+wind;yy=y-round(height*size)
        d.line((x,y,xx,yy),fill='#57805a')
        for j in range(2,6):
            f=j/6;px=round(x+(xx-x)*f);py=round(y+(yy-y)*f)
            length=max(1,round((5-j*.55)*size))
            d.line((px,py,px-side*length,py-2),fill='#40735b')
            d.line((px,py,px+side*length,py-3),fill='#68945d')

def stump(im,x,t):
    d=ImageDraw.Draw(im)
    def poly(p,col):d.polygon([(x+a,b) for a,b in p],fill=col)
    def rect(a,b,w,h,c):d.rectangle((x+a,b,x+a+w-1,b+h-1),fill=c)
    ink='#3b3430'
    # Deep roots, uneven bark and a cut wooden crown.
    poly([(-13,19),(-12,34),(-17,42),(-14,44),(-6,42),(0,44),(7,42),(15,44),(17,41),(12,35),(13,18)],ink)
    poly([(-11,20),(-10,34),(-14,41),(-8,40),(-4,42),(1,41),(6,40),(13,42),(10,35),(11,20)],'#786046')
    rect(-9,23,3,15,'#9b7951');rect(7,22,2,15,'#674936');rect(-3,31,2,9,'#71503a')
    poly([(-12,18),(-8,15),(-1,14),(8,15),(13,18),(11,22),(4,24),(-5,23),(-11,21)],ink)
    poly([(-10,18),(-6,16),(1,15),(8,16),(11,18),(9,21),(3,22),(-5,21)],'#ae9064')
    d.arc((x-7,16,x+8,21),0,340,fill='#8d673f')
    d.arc((x-4,17,x+4,20),10,330,fill='#9d7547')
    rect(0,18,2,1,'#7f5d3e')
    # Eyebrows and two warm eyes open in a slow, living expression.
    opening,gaze=eye_state(t)
    close=opening<.18
    d.line((x-9,25,x-3,27),fill=ink);d.line((x+3,27,x+9,25),fill=ink)
    if close:
        rect(-8,28,5,1,'#332f2b');rect(3,28,5,1,'#332f2b')
    else:
        poly([(-9,27),(-7,26),(-3,27),(-3,30),(-6,31),(-9,30)],'#332f2b')
        poly([(3,27),(7,26),(9,27),(9,30),(6,31),(3,30)],'#332f2b')
        eyeheight=2 if opening>.65 else 1
        rect(-7+gaze,28,2,eyeheight,'#a8aa74');rect(5+gaze,28,2,eyeheight,'#a8aa74')
        rect(-7+gaze,28,1,1,'#c7c6a0');rect(5+gaze,28,1,1,'#c7c6a0')
        if opening<.65:
            rect(-9,27,7,1,'#786046');rect(3,27,7,1,'#786046')
    poly([(-1,29),(-3,33),(1,34),(3,32)],'#684a36')
    d.line([(x-6,37),(x-2,36),(x+2,37),(x+6,36)],fill='#302f2b')
    rect(-2,38,4,1,'#6b503a')
    # Moss, one tiny mushroom and a small fern flower enliven the wood.
    poly([(-13,19),(-9,18),(-6,20),(-3,19),(0,21),(-4,23),(-8,22),(-10,25),(-12,23)],'#537759')
    rect(-10,20,2,1,'#8aa164');rect(-6,21,2,1,'#8aa164')
    poly([(8,34),(11,33),(13,34),(12,37),(9,37)],'#624c39')
    poly([(8,33),(9,31),(12,30),(15,31),(16,33),(13,34),(9,34)],'#956b52')
    rect(10,31,2,1,'#b99570')

def fire(im,x,t,small=False):
    d=ImageDraw.Draw(im);base=42
    d.line((x-7,base,x+6,base-3),fill='#806044',width=2)
    d.line((x-6,base-3,x+7,base),fill='#5b4638',width=2)
    wave=round(math.sin(t*math.tau/3)*2)
    h=15 if small else 25
    d.polygon([(x,base-h+wave),(x+3,base-h+7),(x+4,base-12),(x+7,base-17+wave),(x+8,base-7),(x+5,base-2),(x-4,base-2),(x-8,base-8),(x-5,base-16),(x-3,base-9)],fill='#b56536')
    d.polygon([(x,base-h+5+wave),(x+1,base-11),(x+5,base-12),(x+4,base-5),(x,base-3),(x-4,base-5),(x-5,base-10),(x-2,base-8)],fill='#e5a24f')
    d.polygon([(x,base-14),(x+2,base-8),(x+1,base-4),(x-2,base-5),(x-2,base-9)],fill='#f4d289')
    for i in range(3):
        p=(t/3+i/3)%1;px=x+round(math.sin(p*5+i)*3);py=round(base-h-2-p*9)
        if p<.65:d.point((px,py),fill='#b98d54')

def wreath(im,x,y,t,small=False):
    d=ImageDraw.Draw(im)
    # Fixed diagonally over the stump's rim, with a tiny leaf movement only.
    angle=-1.03;rx=5;ry=3
    points=[]
    for i in range(40):
        a=i*math.tau/40
        xx=rx*math.cos(a);yy=ry*math.sin(a)
        points.append((round(x+xx*math.cos(angle)-yy*math.sin(angle)),round(y+xx*math.sin(angle)+yy*math.cos(angle))))
    d.line(points+[points[0]],fill='#263f34',width=3)
    d.line(points+[points[0]],fill='#779265',width=1)
    # Upper-right woven section rests on and wraps over the stump crown.
    d.line([(x+4,y-3),(x+6,y-4),(x+8,y-3)],fill='#45634c',width=2)
    d.point((x+6,y-4),fill='#8c9d69')
    for i in range(14):
        a=i*math.tau/14;xx=rx*math.cos(a);yy=ry*math.sin(a)
        px=round(x+xx*math.cos(angle)-yy*math.sin(angle));py=round(y+xx*math.sin(angle)+yy*math.cos(angle))
        d.point((px,py),fill='#7d8c5a')
        if i%3==0:
            color=['#ddd8b4','#b87870','#aabcc4'][i%3]
            d.line((px-1,py,px+1,py),fill=color);d.point((px,py-1),fill=color)


def owl_hollow(im,x,t):
    d=ImageDraw.Draw(im)
    d.polygon([(x-4,11),(x-2,9),(x+2,9),(x+4,12),(x+4,20),(x+2,22),(x-3,21),(x-5,18)],fill='#554e3d')
    d.polygon([(x-3,12),(x-1,10),(x+1,10),(x+3,13),(x+3,19),(x+1,21),(x-2,20),(x-3,18)],fill='#222a24')
    opening,gaze=eye_state(t,True)
    if opening<.18:
        d.line((x-2,15,x-1,15),fill='#101915');d.line((x+1,15,x+2,15),fill='#101915')
    else:
        height=3 if opening>.65 else 1
        d.rectangle((x-2,14,x-1,14+height-1),fill='#101915');d.rectangle((x+1,14,x+2,14+height-1),fill='#101915')
        d.point((x-2+gaze,14),fill='#64715c');d.point((x+1+gaze,14),fill='#64715c')
    # Moss curls around the rim; a climbing vine occupies the bark beside it.
    d.line([(x-5,10),(x-3,8),(x+2,8),(x+4,10)],fill='#60805a')
    d.line([(x+5,6),(x+6,14),(x+5,23),(x+6,30)],fill='#3d654b')
    for yy in [7,12,19,26]:
        d.polygon([(x+5,yy),(x+8,yy-2),(x+9,yy),(x+6,yy+1)],fill='#587950')
        d.polygon([(x+5,yy+2),(x+2,yy),(x+2,yy+2),(x+5,yy+4)],fill='#496d4c')


def plants(im,x,y,t,size=1):
    d=ImageDraw.Draw(im);sway=round(math.sin(t*math.tau/12+x)*.6)
    # Layered broad leaves alternate with the fine fern fronds.
    for side,h in [(-1,13),(1,17),(-1,22),(1,9)]:
        tipx=x+round(side*8*size)+sway;tipy=y-round(h*size)
        d.line((x,y,tipx,tipy),fill='#3e6349')
        d.polygon([(tipx,tipy),(tipx-side*5,tipy+2),(tipx-side*3,tipy+6),(tipx+side,tipy+3)],fill='#3d694c')
        d.line((tipx,tipy+1,tipx-side*2,tipy+4),fill='#6d8956')
    fern(im,x,y,-1,t,size*.75)


def reeds(im,x,base,t,height=18):
    d=ImageDraw.Draw(im)
    sway=round(math.sin(t*math.tau/12+x)*1)
    for i in range(4):
        xx=x+i*2;top=base-height+(i%3)*3;end=xx+sway
        d.line((xx,base,end,top),fill='#53694b')
        if i%2==0:d.line((end,top-3,end,top+2),fill='#7b6647',width=2)
        d.line([(xx,base),(xx-3,base-7),(xx-4,base-11)],fill='#3e5b49')
        d.line([(xx,base),(xx+3,base-5),(xx+4,base-9)],fill='#49654a')


def grounded(im,fn,x,t,dy):
    layer=Image.new('RGBA',im.size)
    fn(layer,x,t)
    im.alpha_composite(layer,(0,dy))


def frame(kind,t):
    t=t%24
    w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    # Solid bank and its continuous rim lie ABOVE the narrow foreground water.
    d.polygon([(0,39),(15,37),(w//2,39),(w-20,36),(w-1,38),(w-1,44),(0,44)],fill='#363e30')
    d.line([(0,43),(20,42),(w//2,44),(w-20,42),(w-1,43)],fill='#5b6143')
    d.rectangle((0,44,w-1,45),fill='#2d4345')
    for i in range(w//17):
        xx=(i*21+round(math.sin(t*math.tau/12+i)))%w
        d.line((xx,45,min(w-1,xx+5),45),fill='#405958')
    if kind=='wide':
        # Rear tree roots are embedded in the higher right bank, away from water.
        d.polygon([(112,0),(126,0),(125,27),(128,37),(134,40),(119,40),(116,34)],fill='#3e4939')
        for xx in [116,123]:d.line((xx,2,xx+1,31),fill='#535b42')
        d.line((119,10,105,2),fill='#4c5940',width=2)
        for x,y in [(108,3),(128,5),(133,14)]:
            d.polygon([(x-4,y),(x,y-2),(x+4,y),(x,y+2)],fill='#354f40')
        owl_hollow(im,119,t)
        plants(im,9,42,t,.9);plants(im,43,41,t,1);plants(im,94,41,t,1.05);plants(im,130,40,t,.9)
        reeds(im,97,41,t,22);reeds(im,1,42,t,19)
        fern(im,108,41,-1,t,.9);fern(im,35,42,1,t,.7)
        fern(im,43,41,-1,t,.75);fern(im,131,41,-1,t,.55)
        grounded(im,fire,23,t,-3)
        grounded(im,stump,68,t,-2);wreath(im,57,20,t)
        fern(im,85,42,1,t,.65)
    elif kind=='compact':
        plants(im,5,42,t,.9);plants(im,34,41,t,.9);plants(im,79,41,t,1.05)
        reeds(im,80,41,t,22);reeds(im,1,42,t,17)
        fern(im,73,42,-1,t,.75)
        fern(im,34,42,-1,t,.6)
        grounded(im,fire,18,t,-3)
        grounded(im,stump,52,t,-2);wreath(im,41,20,t)
        fern(im,73,42,1,t,.55)
    else:
        reeds(im,2,42,t,17);fern(im,45,42,-1,t,.75)
        grounded(im,stump,27,t,-2);wreath(im,16,20,t)
    # A few low fireflies keep motion subtle and the upper woodland quiet.
    for i in range(3 if kind=='wide' else 1):
        x=round((i+1)*w/(4 if kind=='wide' else 2)+math.sin(t*math.tau/12+i))
        y=round(9+i%2*7+math.sin(t*math.tau/6+i))
        v=.5+.5*math.sin(t*math.tau/6+i*2)
        color=tuple(round(a+(b-a)*v) for a,b in zip((56,67,47),(119,128,79)))
        d.point((x,y),fill=color)
    return im


def main():
    board=Image.new('RGB',(680,710),BG);d=ImageDraw.Draw(board)
    for j,(kind,w) in enumerate(SIZES.items()):
        frames=[]
        for i in range(240):
            art=frame(kind,i/10);out=Image.new('RGB',art.size,BG);out.paste(art,mask=art.getchannel('A'))
            frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'kupala-{kind}-v6.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
        art=frame(kind,2.5);art.save(ROOT/f'kupala-{kind}-v6.png')
        d.text((20,15+j*230),kind.upper(),fill='#c7c9b7')
        art=art.resize((w*4,184),Image.Resampling.NEAREST);board.paste(art,(20,42+j*230),art)
        print(kind,(ROOT/f'kupala-{kind}-v6.gif').stat().st_size)
    board.save(ROOT/'contact-sheet-v6.png')

if __name__=='__main__':main()
