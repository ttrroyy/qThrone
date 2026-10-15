"""Valentine code-native pixel studies: two living cats sharing one heart."""
from pathlib import Path
from PIL import Image, ImageDraw
import math

ROOT=Path(__file__).parent
SIZES={'wide':136,'compact':92,'narrow':48}
BG='#202020'

def ease(v):
    v=max(0,min(1,v));return v*v*(3-2*v)

def heart(im,x,y,r,color='#b76a75'):
    d=ImageDraw.Draw(im)
    pts=[(-1,0),(-1,-.55),(-.65,-1),(-.25,-1),(0,-.6),(.25,-1),(.65,-1),(1,-.55),(1,0),(.65,.45),(0,1),(-.65,.45)]
    d.polygon([(round(x+a*r),round(y+b*r)) for a,b in pts],fill='#49333e')
    rr=max(1,r-1)
    d.polygon([(round(x+a*rr),round(y+b*rr)) for a,b in pts],fill=color)
    if r>=4:d.line((x-round(r*.5),y-round(r*.5),x-round(r*.3),y-round(r*.6)),fill='#e2a6aa')

def rose(im,x,y,t,seed=0,size=4):
    d=ImageDraw.Draw(im);sway=round(math.sin(t*math.tau/12+seed)*.6)
    xx=x+sway
    d.line((x,y,xx,y-7),fill='#4e6750')
    d.polygon([(x,y-2),(x-4,y-5),(x-3,y-2)],fill='#587650')
    d.polygon([(x,y-1),(x+4,y-4),(x+3,y-1)],fill='#3d5d49')
    cy=y-9
    pts=[(-3,-2),(-1,-4),(2,-3),(4,-1),(3,2),(1,3),(-2,3),(-4,1),(-4,-1)]
    scale=size/4
    d.polygon([(round(xx+a*scale),round(cy+b*scale)) for a,b in pts],fill='#613b49')
    d.polygon([(xx-2,cy-2),(xx+1,cy-2),(xx+2,cy),(xx+1,cy+2),(xx-2,cy+1)],fill='#a95b6d')
    d.line([(xx-1,cy-1),(xx+1,cy-1),(xx+1,cy),(xx-1,cy+1)],fill='#cf8a92')

def candle(im,x,t):
    d=ImageDraw.Draw(im)
    d.rectangle((x,29,x+5,42),fill='#624a45');d.rectangle((x+1,29,x+4,41),fill='#d8c2a3')
    d.line((x+1,30,x+1,40),fill='#f0dfb9')
    d.line((x+3,26,x+3,29),fill='#493633')
    tip=20+round(math.sin(t*math.tau/3))
    d.polygon([(x+3,tip),(x+6,25),(x+4,27),(x+1,27),(x,25)],fill='#cc9258')
    d.polygon([(x+3,tip+3),(x+4,25),(x+3,27),(x+2,25)],fill='#f4db9f')

def cats(im,c,t):
    d=ImageDraw.Draw(im)
    # Closeness is held, then released slowly; all movement is an integer pixel.
    cuddle=ease((t-7)/2)*(1-ease((t-16)/2))
    shift=round(cuddle)
    palettes=[('#39363e','#74717a','#a19aa0','#b98c91'),('#6c5047','#bd9979','#ddbc91','#c88c85')]
    for side,(ink,body,light,ear) in zip([-1,1],palettes):
        x=c+side*(8-shift)
        def poly(p,col):d.polygon([(x+a,b) for a,b in p],fill=col)
        def rect(a,b,w,h,col):d.rectangle((x+a,b,x+a+w-1,b+h-1),fill=col)
        # Tail curls outside the pair and merges into the seated body.
        poly([(side*5,34),(side*10,35),(side*12,39),(side*11,43),(side*5,44),(side*2,42)],ink)
        d.line([(x+side*5,36),(x+side*9,37),(x+side*10,40),(x+side*8,42),(x+side*5,41)],fill=body,width=2)
        poly([(-5,29),(-7,35),(-6,41),(-3,44),(4,44),(7,41),(6,34),(4,29)],ink)
        poly([(-4,30),(-5,35),(-4,41),(-2,43),(3,43),(5,40),(4,34),(3,30)],body)
        poly([(-2,33),(-3,39),(-1,42),(2,42),(3,37),(1,33)],light)
        # Distinct cat ears with soft inner colour, no human facial features.
        poly([(-7,23),(-7,13),(-2,17),(3,17),(7,13),(8,24),(6,29),(1,32),(-4,30)],ink)
        poly([(-6,22),(-6,16),(-2,19),(3,19),(6,16),(6,24),(4,28),(0,30),(-4,28)],body)
        poly([(-5,18),(-5,21),(-2,20)],ear);poly([(5,18),(3,21),(5,22)],ear)
        rect(-5,23,2,3,light)
        phase=(t+(1.4 if side>0 else 0))%24
        closed=(2.2<phase<2.5 or 5.6<phase<6.0 or 11<phase<14 or 20<phase<20.3)
        if cuddle>.5 and 9<t<16:closed=True
        gaze=-side if 3<t<19 else 0
        if closed:
            d.line((x-4,24,x-2,25),fill=ink);d.line((x+1,25,x+4,24),fill=ink)
        else:
            rect(-4+gaze,23,2,2,'#c5c29a');rect(2+gaze,23,2,2,'#c5c29a')
            rect(-3+gaze,23,1,2,ink);rect(3+gaze,23,1,2,ink)
        poly([(-1,26),(1,26),(0,27)],'#c58d91')
        d.line((x,27,x,28),fill=ink)
        d.line((x-2,28,x,29,x+2,28),fill=ink)
        # Whiskers remain short so the two faces do not merge.
        d.line((x-7,26,x-5,26),fill=light);d.line((x+5,26,x+7,26),fill=light)
    r=5+(.35*math.sin(t*math.tau/3))
    heart(im,c,36,r)
    # Both sets of paws support the shared heart from the sides.
    d.line((c-7,36,c-4,38),fill='#a19aa0',width=2)
    d.line((c+7,36,c+4,38),fill='#ddbc91',width=2)

def envelope(im,x,y):
    d=ImageDraw.Draw(im)
    d.rectangle((x,y,x+16,y+10),fill='#6a4e50');d.rectangle((x+1,y+1,x+15,y+9),fill='#d0b9a5')
    d.line([(x+1,y+1),(x+8,y+6),(x+15,y+1)],fill='#a4837d')
    d.line([(x+1,y+9),(x+6,y+5)],fill='#b29989');d.line([(x+15,y+9),(x+10,y+5)],fill='#b29989')
    heart(im,x+8,y+5,2,'#a65c6c')

def frame(kind,t):
    t=t%24;w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    # A soft low textile base, not a full decorative background.
    d.polygon([(1,44),(7,41),(w//2,40),(w-9,41),(w-1,44),(w-1,45),(1,45)],fill='#493d44')
    d.line((4,44,w-5,44),fill='#6a505b')
    if kind=='wide':
        for i,(x,y) in enumerate([(7,44),(18,43),(39,44),(96,44),(110,44),(129,44)]):rose(im,x,y,t,i)
        candle(im,28,t);envelope(im,105,32)
        # A small wrapped gift nestles in the roses at left.
        d.rectangle((43,35,55,43),fill='#8c596b');d.rectangle((42,34,56,36),fill='#b57a85')
        d.rectangle((48,34,50,43),fill='#ccb39c')
        d.line([(49,34),(45,31),(44,32),(45,34),(49,34),(53,31),(55,32),(54,34)],fill='#ccb39c')
        cats(im,76,t)
    elif kind=='compact':
        for i,x in enumerate([6,19,71,87]):rose(im,x,44,t,i)
        envelope(im,9,34);cats(im,48,t)
    else:
        rose(im,3,44,t,2,3);rose(im,45,44,t,5,3)
        cats(im,24,t)
    # A single tiny rising heart is occasional and stays inside the decoration.
    if kind!='narrow':
        c=76 if kind=='wide' else 48
        phase=t%12
        if 7<phase<10:
            yy=round(14-(phase-7)*2)
            heart(im,c,yy,2,'#a16a7b')
    return im

def main():
    board=Image.new('RGB',(680,710),BG);d=ImageDraw.Draw(board)
    for j,(kind,w) in enumerate(SIZES.items()):
        frames=[]
        for i in range(240):
            art=frame(kind,i/10);out=Image.new('RGB',art.size,BG);out.paste(art,mask=art.getchannel('A'))
            frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'valentine-{kind}.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
        art=frame(kind,3.5);art.save(ROOT/f'valentine-{kind}.png')
        d.text((20,15+j*230),kind.upper(),fill='#cabfc5')
        art=art.resize((w*4,184),Image.Resampling.NEAREST);board.paste(art,(20,42+j*230),art)
        assert frame(kind,0).tobytes()==frame(kind,24).tobytes()
        print(kind,(ROOT/f'valentine-{kind}.gif').stat().st_size)
    board.save(ROOT/'contact-sheet.png')

if __name__=='__main__':main()
