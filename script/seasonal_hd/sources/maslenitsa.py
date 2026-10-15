"""Maslenitsa native pixel studies: living straw effigy, pancakes, samovar and winter thaw."""
from pathlib import Path
from PIL import Image, ImageDraw
import math

ROOT=Path(__file__).parent
SIZES={'wide':136,'compact':92,'narrow':48}
BG='#202020'

def ease(v):
    v=max(0,min(1,v));return v*v*(3-2*v)

def eye_state(t):
    closed=0
    for a,hold in [(2.3,.18),(7.4,1.3),(13,.2),(18.4,1.8),(22.3,.18)]:
        closed=max(closed,ease((t-a)/.2)*(1-ease((t-a-.2-hold)/.3)))
    keys=[(0,0),(3,0),(3.6,-1),(5.5,-1),(6.1,0),(11,0),(11.7,1),(14.1,1),(14.8,0),(24,0)]
    gaze=0
    for (a,x),(b,y) in zip(keys,keys[1:]):
        if a<=t<=b:gaze=round(x+(y-x)*ease((t-a)/(b-a)));break
    return 1-closed,gaze

def straw(im,x,y,t,size=1):
    d=ImageDraw.Draw(im);sway=round(math.sin(t*math.tau/12+x)*.7)
    for dx,h,col in [(-7,9,'#8f784c'),(-4,14,'#b49a62'),(0,17,'#cfb77a'),(4,13,'#967c49'),(7,8,'#baa46a')]:
        xx=x+round(dx*size)+sway;yy=y-round(h*size)
        d.line((x,y,xx,yy),fill=col)
        if h>10:
            for j in range(3):
                p=j*2
                d.line((xx,yy+p,xx-2,yy+p+1),fill='#c2aa6f')
                d.line((xx,yy+p+1,xx+2,yy+p+2),fill='#a58a52')
    d.line((x-2,y-4,x+2,y-4),fill='#8b5943')

def effigy(im,c,t):
    d=ImageDraw.Draw(im)
    sway=round(math.sin(t*math.tau/12)*.7)
    c+=sway
    def poly(p,col):d.polygon([(c+x,y) for x,y in p],fill=col)
    def rect(x,y,w,h,col):d.rectangle((c+x,y,c+x+w-1,y+h-1),fill=col)
    ink='#493932'
    # Dark wooden support; the skirt has a clean edge without a bright fringe.
    rect(-1,31,3,14,'#785538')
    # Crossbar, straw hands and broad cloth sleeves form a clear scarecrow silhouette.
    d.line((c-17,24,c+17,24),fill='#76523b',width=2)
    poly([(-5,22),(-11,21),(-17,23),(-16,28),(-10,27),(-5,28)],ink)
    poly([(5,22),(11,21),(17,23),(16,28),(10,27),(5,28)],ink)
    poly([(-6,23),(-12,22),(-16,24),(-14,26),(-9,25),(-5,27)],'#a0584b')
    poly([(6,23),(12,22),(16,24),(14,26),(9,25),(5,27)],'#a0584b')
    for side in [-1,1]:
        for j in range(4):
            d.line((c+side*16,24+j%2,c+side*(19+j%2),22+j),fill='#c3a269')
        rect(side*15-1,23,2,4,'#c8a570')
    # Layered skirt: woven red fabric, apron and small geometric embroidery.
    poly([(-7,23),(-8,29),(-14,40),(-12,43),(12,43),(14,40),(8,29),(7,23)],ink)
    poly([(-6,24),(-7,30),(-12,40),(-10,42),(10,42),(12,40),(7,30),(6,24)],'#9a4e42')
    poly([(-5,29),(-8,40),(-5,41),(-2,30)],'#b36b50')
    poly([(5,29),(8,40),(5,41),(2,30)],'#783e37')
    poly([(-4,28),(-6,39),(0,41),(6,39),(4,28)],'#c8ad7b')
    rect(-5,27,10,2,'#7e493b')
    for xx in [-3,0,3]:
        d.point((c+xx,36),fill='#8d5844');d.point((c+xx,38),fill='#8d5844')
    d.line((c-8,40,c+8,40),fill='#a98459')
    # Kerchief surrounds a straw face, with a little tie at the neck.
    poly([(-9,8),(-5,4),(3,3),(8,6),(11,13),(8,21),(5,24),(-6,24),(-11,18)],ink)
    poly([(-8,9),(-4,5),(3,4),(7,7),(9,13),(6,20),(4,22),(-5,22),(-9,17)],'#9e554b')
    poly([(-6,10),(-3,8),(3,8),(6,11),(6,17),(3,21),(-3,21),(-6,17)],'#c5a66d')
    rect(-5,11,2,6,'#ddc38a');rect(4,11,1,6,'#a78652')
    for xx,yy in [(-5,6),(3,5),(7,11),(-8,14),(7,17),(-6,20)]:rect(xx,yy,1,1,'#d7b381')
    opening,gaze=eye_state(t)
    d.line((c-5,12,c-2,13),fill='#7e633f');d.line((c+2,13,c+5,12),fill='#7e633f')
    if opening<.2:
        rect(-5,15,3,1,ink);rect(2,15,3,1,ink)
    else:
        h=2 if opening>.65 else 1
        rect(-4+gaze,14,1,h,'#4a4532');rect(3+gaze,14,1,h,'#4a4532')
    poly([(-1,15),(-2,17),(1,18),(2,17)],'#ab8952')
    d.line((c-3,19,c+2,20,c+4,19),fill='#795139')
    rect(-5,17,2,1,'#bf8660');rect(4,17,2,1,'#bf8660')
    poly([(0,21),(-3,23),(-1,25),(1,23),(5,25),(4,22)],'#ae6b55')

def pancakes(im,x,y):
    d=ImageDraw.Draw(im)
    d.ellipse((x-12,y-2,x+12,y+2),fill='#795b3e')
    d.line((x-10,y+1,x+10,y+1),fill='#b89b69')
    for i in range(5):
        yy=y-2-i*2
        d.ellipse((x-10,yy-2,x+10,yy+1),fill='#9d6b3e')
        d.line((x-8,yy-1,x+8,yy-1),fill='#d5ae6b')
    d.ellipse((x-9,y-12,x+9,y-9),fill='#dfbd7d')
    d.rectangle((x-2,y-13,x+2,y-11),fill='#e2cf8e')
    d.line((x-1,y-10,x+1,y-7),fill='#ae7b42')

def samovar(im,x,t):
    d=ImageDraw.Draw(im)
    d.rectangle((x-2,20,x+2,26),fill='#846a44')
    d.line((x-3,20,x+3,20),fill='#c3aa70')
    d.polygon([(x-7,27),(x-5,24),(x+5,24),(x+7,27),(x+6,35),(x+3,38),(x-3,38),(x-6,35)],fill='#705a3d')
    d.polygon([(x-5,27),(x-3,25),(x+3,25),(x+5,28),(x+4,34),(x+2,36),(x-2,36),(x-4,34)],fill='#af925a')
    d.line((x-3,27,x-3,33),fill='#d8c084')
    d.line([(x-6,27),(x-9,27),(x-9,32),(x-6,33)],fill='#9a7b4e')
    d.line([(x+6,27),(x+9,27),(x+9,32),(x+6,33)],fill='#9a7b4e')
    d.line((x+6,34,x+10,34,x+10,32),fill='#b59a64')
    d.rectangle((x-2,38,x+2,40),fill='#967744');d.rectangle((x-6,41,x+6,42),fill='#967744')
    for k in range(2):
        phase=(t/3+k*.5)%1
        yy=round(18-phase*9);xx=x+round(math.sin(phase*5+k))
        if phase<.85:
            d.line([(xx,yy+2),(xx-1,yy),(xx+1,yy-2)],fill='#807f6c' if phase<.5 else '#555e51')

def bagels(im,x,y,t):
    d=ImageDraw.Draw(im)
    d.line([(x+4,y-15),(x-1,y-11),(x-4,y-5),(x,y),(x+6,y-5),(x+7,y-11),(x+4,y-15)],fill='#7c6746')
    for xx,yy in [(-2,-12),(-4,-8),(-3,-4),(1,-2),(4,-6),(3,-11)]:
        xx+=round(-yy/15*4)
        d.ellipse((x+xx-2,y+yy-2,x+xx+2,y+yy+2),fill='#bc955b')
        d.point((x+xx,y+yy),fill='#4b4232')
        d.point((x+xx-1,y+yy-1),fill='#dfbd7a')

def rustic_backdrop(im,w):
    d=ImageDraw.Draw(im)
    # Low woven fence and hay give the props a shared grounded setting.
    for xx in range(3,w,9):
        d.line((xx,29+(xx%3),xx,43),fill='#6d5940')
        d.point((xx,28+(xx%3)),fill='#9c8154')
    for yy in [34,38,41]:
        points=[(xx,yy+(i%2)) for i,xx in enumerate(range(0,w,4))]
        d.line(points,fill='#7b6547')


def jar(im,x,y):
    d=ImageDraw.Draw(im)
    d.polygon([(x-3,y-9),(x+3,y-9),(x+4,y-6),(x+4,y-1),(x+2,y),(x-2,y),(x-4,y-1),(x-4,y-6)],fill='#79523b')
    d.line((x-2,y-7,x-2,y-2),fill='#b68c59')
    d.rectangle((x-3,y-10,x+3,y-8),fill='#c2a474')
    d.line((x-3,y-8,x+3,y-8),fill='#9d6445')


def logs(im,x,y):
    d=ImageDraw.Draw(im)
    for dx,dy in [(-3,0),(3,0),(0,-3)]:
        d.line((x+dx-5,y+dy,x+dx+3,y+dy-2),fill='#6c5038',width=3)
        d.ellipse((x+dx+1,y+dy-4,x+dx+5,y+dy),fill='#a48453')
        d.point((x+dx+3,y+dy-2),fill='#70513a')


def frame(kind,t):
    t=t%24;w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    d.polygon([(0,43),(9,40),(24,42),(w//2,40),(w-12,41),(w-1,43),(w-1,45),(0,45)],fill='#474737')
    if kind!='narrow':rustic_backdrop(im,w)
    if kind=='wide':
        for i,(x,y,s) in enumerate([(5,44,1.15),(38,44,.9),(91,44,1.05),(131,43,1.2)]):straw(im,x,y,t,s)
        # Low rustic serving bench with feet set on the ground.
        d.rectangle((13,40,47,42),fill='#74503a');d.line((16,42,15,45),fill='#573e30');d.line((44,42,45,45),fill='#573e30')
        pancakes(im,28,39)
        samovar(im,112,t);bagels(im,100,44,t)
        jar(im,47,41)
        effigy(im,70,t)
        straw(im,51,45,t,.5);straw(im,86,45,t,.6)
    elif kind=='compact':
        straw(im,4,44,t,1);straw(im,32,45,t,.85);straw(im,87,44,t,1.1)
        d.rectangle((5,41,30,43),fill='#74503a');pancakes(im,18,40)
        samovar(im,81,t);bagels(im,69,44,t)
        jar(im,31,42);effigy(im,52,t)
        straw(im,73,45,t,.55)
    else:
        straw(im,4,44,t,.65);straw(im,44,44,t,.65)
        effigy(im,25,t)
        for xx in [2,39]:d.line((xx,44,xx+5,43),fill='#b79860')
    return im

def main():
    board=Image.new('RGB',(680,710),BG);d=ImageDraw.Draw(board)
    for j,(kind,w) in enumerate(SIZES.items()):
        frames=[]
        for i in range(240):
            art=frame(kind,i/10);out=Image.new('RGB',art.size,BG);out.paste(art,mask=art.getchannel('A'))
            frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'maslenitsa-{kind}-v2.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
        art=frame(kind,3.5);art.save(ROOT/f'maslenitsa-{kind}-v2.png')
        d.text((20,15+j*230),kind.upper(),fill='#cac3ae')
        art=art.resize((w*4,184),Image.Resampling.NEAREST);board.paste(art,(20,42+j*230),art)
        assert frame(kind,0).tobytes()==frame(kind,24).tobytes()
        print(kind,(ROOT/f'maslenitsa-{kind}-v2.gif').stat().st_size)
    board.save(ROOT/'contact-sheet-v2.png')

if __name__=='__main__':main()
