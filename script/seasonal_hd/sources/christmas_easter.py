"""Native pixel previews: Christmas distinct from New Year, and living Easter bunny."""
from pathlib import Path
import math
from PIL import Image, ImageDraw

BASE=Path(__file__).parent
SIZES={'wide':136,'compact':92,'narrow':48}
BG='#202020'

def ease(v):
    v=max(0,min(1,v));return v*v*(3-2*v)

def eyes(t):
    closed=0
    for a,h in [(2.2,.18),(7.4,1.2),(13,.2),(18.2,1.7),(22.2,.16)]:
        closed=max(closed,ease((t-a)/.2)*(1-ease((t-a-.2-h)/.3)))
    gaze=-1 if 3.7<t<5.8 else 1 if 11.8<t<14.4 else 0
    return 1-closed,gaze

def egg(im,x,y,color='#bfa0ac',size=1,pattern=0):
    d=ImageDraw.Draw(im);r=round(4*size);h=round(6*size)
    d.polygon([(x-r,y),(x-r+1,y-h+2),(x-1,y-h),(x+1,y-h),(x+r-1,y-h+2),(x+r,y),(x+r-1,y+3),(x-r+1,y+3)],fill='#5b5650')
    d.polygon([(x-r+1,y),(x-r+2,y-h+3),(x-1,y-h+1),(x+1,y-h+1),(x+r-2,y-h+3),(x+r-1,y),(x+r-2,y+2),(x-r+2,y+2)],fill=color)
    if pattern%2:
        for xx in [-2,0,2]:d.point((x+xx,y-1),fill='#e9d2a0')
        d.line((x-r+1,y+1,x+r-1,y+1),fill='#e1c78e')
    else:
        d.line((x-r+1,y-2,x+r-1,y-2),fill='#ded3ad')
        for xx in [-1,1]:d.point((x+xx,y),fill='#ece0bb')
    d.point((x-r+2,y-h+3),fill='#e7d8bd')

def grass(im,x,y,t,flower=False,seed=0):
    d=ImageDraw.Draw(im);sway=round(math.sin(t*math.tau/12+seed)*.7)
    for dx,h in [(-5,5),(-3,9),(0,11),(3,7),(5,4)]:
        d.line((x,y,x+dx+sway,y-h),fill=['#557951','#698850','#43694b'][seed%3])
    if flower:
        xx=x+sway;yy=y-11
        for dx,dy in [(0,-2),(-2,0),(2,0),(0,2)]:d.rectangle((xx+dx,yy+dy,xx+dx+1,yy+dy+1),fill='#e0c880' if seed%2 else '#c2acc1')
        d.rectangle((xx,yy,xx+1,yy+1),fill='#a58b4e')

def willow(im,x,y,t):
    d=ImageDraw.Draw(im);s=round(math.sin(t*math.tau/12)*.8)
    for dx,h in [(-6,22),(0,29),(6,24)]:
        xx=x+dx+s
        d.line((x,y,xx,y-h),fill='#786f4f')
        for j in range(4):
            f=.4+j*.16;px=round(x+(xx-x)*f);py=round(y-h*f)
            d.ellipse((px-1,py-1,px+1,py+1),fill='#bfc2a6')
            d.point((px-1,py-1),fill='#e4dfc3')

def basket(im,x,t):
    d=ImageDraw.Draw(im)
    d.arc((x-12,17,x+12,39),180,360,fill='#9d8056',width=2)
    d.line((x-12,28,x-12,35),fill='#9d8056',width=2)
    d.line((x+12,28,x+12,35),fill='#9d8056',width=2)
    for i,(xx,yy) in enumerate([(-7,31),(-1,29),(6,31)]):egg(im,x+xx,yy,['#afa7bf','#c1aa75','#9eb8a6'][i],1,i)
    d.polygon([(x-14,33),(x+14,33),(x+11,43),(x-11,43)],fill='#7d6545')
    d.line((x-13,33,x+13,33),fill='#c1a273',width=2)
    for yy in [36,39,42]:d.line((x-11,yy,x+11,yy),fill='#a58a5e')
    for xx in [-8,-3,3,8]:d.line((x+xx,35,x+xx,42),fill='#b39868')

def bunny(im,c,t):
    d=ImageDraw.Draw(im)
    def poly(p,col):d.polygon([(c+x,y) for x,y in p],fill=col)
    def rect(x,y,w,h,col):d.rectangle((c+x,y,c+x+w-1,y+h-1),fill=col)
    ink='#55574e';fur='#a9a899';light='#d9d1b6';shade='#8a9082'
    # Long ears, one gently bent, in the same full-height character at all widths.
    sway=round(math.sin(t*math.tau/12)*.6)
    poly([(-8,20),(-10,9),(-9,4),(-6,3),(-3,7),(-2,18)],ink)
    poly([(-7,18),(-8,9),(-8,5),(-6,5),(-5,8),(-4,18)],fur)
    d.line([(c-7,7),(c-6,12),(c-5,17)],fill='#b29390',width=2)
    poly([(1,18),(2+sway,7),(5+sway,2),(8+sway,3),(9+sway,8),(6,20)],ink)
    poly([(3,18),(4+sway,7),(6+sway,4),(7+sway,5),(7+sway,9),(5,18)],light)
    d.line([(c+6+sway,6),(c+5+sway,12),(c+4,17)],fill='#bb9e96')
    d.ellipse((c+8,35,c+14,41),fill=light)
    poly([(-7,29),(-10,35),(-9,41),(-5,44),(7,44),(10,40),(8,33),(5,29)],ink)
    poly([(-6,30),(-8,35),(-7,41),(-4,43),(5,43),(8,39),(6,33)],fur)
    poly([(-3,32),(-5,38),(-2,42),(3,42),(5,37),(3,32)],light)
    poly([(-9,23),(-7,18),(-2,16),(4,17),(8,20),(10,25),(7,29),(1,32),(-6,30)],ink)
    poly([(-7,23),(-5,19),(-1,18),(4,19),(6,21),(8,25),(5,28),(0,30),(-5,28)],fur)
    rect(-6,21,2,5,light);rect(6,24,1,3,shade)
    opening,gaze=eyes(t)
    if opening<.2:
        rect(-5,24,3,1,'#414d44');rect(3,24,3,1,'#414d44')
    else:
        h=3 if opening>.65 else 1
        rect(-4+gaze,22,2,h,'#414d44');rect(4+gaze,22,2,h,'#414d44')
        rect(-4+gaze,22,1,1,'#ded8b9');rect(4+gaze,22,1,1,'#ded8b9')
    sniff=round((.5+.5*math.sin(t*math.tau/3))*.7)
    poly([(-1,25-sniff),(2,25-sniff),(0,27-sniff)],'#b98d8e')
    d.line((c,27,c,28),fill='#626052');d.line((c-2,28,c,29,c+2,28),fill='#626052')
    d.line((c-9,26,c-5,26),fill='#c9c8b0');d.line((c+5,26,c+9,26),fill='#c9c8b0')
    egg(im,c,38,'#b3b79a',1.1,1)
    d.line((c-7,34,c-4,37),fill=light,width=2);d.line((c+7,34,c+4,37),fill=light,width=2)
    d.ellipse((c-10,41,c-3,44),fill=light);d.ellipse((c+3,41,c+10,44),fill=light)

def gift(im,x,y,color='#b74751'):
    d=ImageDraw.Draw(im);d.rectangle((x-6,y-8,x+6,y),fill=color)
    d.rectangle((x-7,y-9,x+7,y-7),fill='#db7565');d.rectangle((x-1,y-9,x+1,y),fill='#f0d590')
    d.line([(x,y-9),(x-4,y-12),(x-5,y-11),(x-4,y-9),(x,y-9),(x+4,y-12),(x+5,y-11),(x+4,y-9)],fill='#f0d590')

def stocking(im,x,y,t,seed=0):
    d=ImageDraw.Draw(im);s=round(math.sin(t*math.tau/12+seed)*.6)
    d.line((x,y-2,x,y+1),fill='#a99771')
    d.polygon([(x-2,y),(x+3,y),(x+3+s,y+8),(x+6+s,y+9),(x+6+s,y+12),(x+1+s,y+13),(x-2+s,y+10)],fill='#83383f')
    d.polygon([(x-1,y+2),(x+2,y+2),(x+2+s,y+9),(x+5+s,y+10),(x+5+s,y+11),(x+1+s,y+12),(x-1+s,y+10)],fill='#ce5553')
    d.rectangle((x-3,y,x+3,y+2),fill='#f0dfb8')
    d.point((x+1+s,y+7),fill='#ceaa76')

def hearth(im,x,t):
    d=ImageDraw.Draw(im)
    d.rectangle((x-14,22,x+14,43),fill='#71503d')
    d.rectangle((x-10,27,x+10,43),fill='#312e2b')
    for yy in [26,31,36,41]:
        d.line((x-14,yy,x-11,yy),fill='#aa7953');d.line((x+11,yy,x+14,yy),fill='#aa7953')
    d.rectangle((x-16,21,x+16,23),fill='#a67b50')
    d.rectangle((x-17,42,x+17,44),fill='#8b6948')
    f=round(math.sin(t*math.tau/3))
    d.polygon([(x,29+f),(x+3,35),(x+6,32),(x+7,39),(x+3,41),(x-5,41),(x-7,37),(x-4,31),(x-2,36)],fill='#e29c43')
    d.polygon([(x,34),(x+3,38),(x+1,41),(x-3,39)],fill='#ffe2a0')
    # Festive evergreen trim belongs on the mantel, never loose on the floor.
    for j in range(13):
        xx=x-14+j*2.2;yy=21+.35*math.sin(j)
        d.line((xx,yy,xx-1,yy-1.5),fill='#477a4a')
        d.line((xx,yy,xx+1,yy-1.4),fill='#609654')
        if j%3==0:
            glow=.5+.5*math.sin(t*math.tau/3+j)
            d.ellipse((xx-.5,yy-.5,xx+.5,yy+.5),fill=tuple(round(a+(b-a)*glow) for a,b in zip((177,92,50),(255,222,131))))
    stocking(im,x-8,23,t);stocking(im,x+6,23,t,3)
    # Evergreen wreath on the wall above the hearth.
    d.ellipse((x-7,5,x+7,17),outline='#3f8051',width=2)
    for a,b in [(-5,7),(3,6),(-6,12),(6,12),(-2,16)]:d.point((x+a,b),fill='#df6254')
    d.line([(x,15),(x-3,18),(x,17),(x+3,18),(x,15)],fill='#d65b51')

def tree(im,c,t):
    d=ImageDraw.Draw(im)
    def poly(p,col):d.polygon([(c+x,y) for x,y in p],fill=col)
    d.rectangle((c-3,35,c+3,44),fill='#6f523b')
    poly([(0,5),(-8,16),(-5,16),(-14,28),(-9,28),(-21,40),(21,40),(9,28),(14,28),(5,16),(8,16)],'#244b35')
    poly([(0,8),(-5,16),(-2,16),(-11,27),(-6,27),(-17,38),(17,38),(6,27),(11,27),(2,16),(5,16)],'#377b49')
    poly([(0,8),(-1,19),(-5,19),(-4,25),(-11,34),(-6,34),(-9,38),(-16,38),(-6,27),(-11,27),(-2,16),(-5,16)],'#61a463')
    poly([(0,1),(2,4),(6,4),(3,7),(4,11),(0,9),(-4,11),(-3,7),(-6,4),(-2,4)],'#f0c965')
    d.line([(c-5,17),(c+5,20),(c-9,27),(c+12,33),(c-4,37)],fill='#d7b56b')
    for i,(xx,yy) in enumerate([(-3,15),(3,17),(4,21),(-5,22),(-7,25),(2,26),(6,27),(-11,34),(-4,33),(2,34),(12,36),(6,37)]):
        glow=(.5+.5*math.sin(t*math.tau/3+i*.85))**1.5
        lamp=[(255,214,112),(255,108,91),(158,222,127),(141,196,231)][i%4]
        halo=tuple(round(v*(.38+.35*glow)) for v in lamp)
        d.ellipse((c+xx-.65,yy-.65,c+xx+1.65,yy+1.65),fill=halo)
        color=tuple(round(v*(.43+.57*glow)) for v in lamp)
        d.ellipse((c+xx-.15,yy-.15,c+xx+1.15,yy+1.15),fill=color)
        if glow>.72:d.ellipse((c+xx+.3,yy+.2,c+xx+.65,yy+.55),fill='#fff5d7')
    for i,(xx,yy) in enumerate([(1,18),(-5,30),(8,33),(-2,37)]):
        d.ellipse((c+xx-1.1,yy-1.1,c+xx+1.1,yy+1.1),fill=['#cf5a52','#e1bc66','#8fbed0'][i%3])
        d.point((c+xx-.4,yy-.5),fill='#f5dfb0')

def lantern(im,x,t):
    d=ImageDraw.Draw(im)
    # The plinth sits directly on the ground at y=44.
    d.ellipse((x-7,43,x+7,45),fill='#39362b')
    d.arc((x-3,20,x+3,26),180,360,fill='#8a7554')
    d.line((x-3,23,x-3,26),fill='#8a7554')
    d.line((x+3,23,x+3,26),fill='#8a7554')
    d.polygon([(x,23),(x+6,28),(x+5,28),(x+5,42),(x+6,44),(x-6,44),(x-5,42),(x-5,28),(x-6,28)],fill='#69583e')
    d.rectangle((x-3,29,x+3,41),fill='#b8934f')
    d.rectangle((x-2,33,x+1,41),fill='#f5d694')
    d.point((x,31+round(math.sin(t*math.tau/3))),fill='#edce8e')
    d.line((x,27,x,42),fill='#66563c')
    d.line((x-6,44,x+6,44),fill='#907a52',width=1)


def evergreen(im,x,y,side):
    d=ImageDraw.Draw(im)
    d.line((x,y,x+side*15,y-4),fill='#466449')
    for i in range(1,6):
        xx=x+side*i*2;yy=y-round(i*.6)
        d.line((xx,yy,xx-side*1,yy-4),fill='#385e43')
        d.line((xx,yy,xx-side*2,yy+2),fill='#547a4b')

def frame(theme,kind,t):
    t=t%24;w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    if theme=='easter':
        d.polygon([(0,44),(8,41),(w//2,40),(w-8,41),(w-1,44),(w-1,45),(0,45)],fill='#3f5b3f')
        for x in range(2,w,7):d.line((x,44,x+3,43),fill='#74875a')
        if kind=='wide':
            willow(im,119,43,t);willow(im,7,43,t)
            for i,x in enumerate([5,42,54,96,110,131]):grass(im,x,45,t,True,i)
            basket(im,26,t);bunny(im,76,t)
            for i,(x,y) in enumerate([(49,41),(99,41),(109,43)]):egg(im,x,y,['#bfa3af','#b9b17a','#9fb5ad'][i],.8,i)
        elif kind=='compact':
            willow(im,84,43,t)
            for i,x in enumerate([4,34,72,88]):grass(im,x,45,t,True,i)
            basket(im,17,t);bunny(im,53,t);egg(im,76,41,'#bfa3af',.8)
        else:
            for i,x in enumerate([3,45]):grass(im,x,45,t,True,i)
            bunny(im,25,t);egg(im,7,42,'#bfa3af',.7);egg(im,43,42,'#b9b17a',.7,1)
    else:
        d.polygon([(0,44),(10,42),(w//2,42),(w-8,42),(w-1,44),(w-1,45),(0,45)],fill='#463d32')
        if kind=='wide':
            hearth(im,22,t)
            tree(im,76,t);lantern(im,119,t)
            gift(im,51,44);gift(im,95,44,'#528d5c');gift(im,106,44,'#c17351')
        elif kind=='compact':
            hearth(im,17,t);tree(im,55,t);lantern(im,84,t)
            gift(im,34,44);gift(im,73,44,'#528d5c')
        else:
            tree(im,25,t);stocking(im,7,27,t);gift(im,40,44)
            evergreen(im,1,45,1);evergreen(im,35,45,1)
    return im

def main():
    for theme in ['christmas','easter']:
        root=BASE/f'{theme}-responsive-v1'
        board=Image.new('RGB',(680,710),BG);d=ImageDraw.Draw(board)
        for j,(kind,w) in enumerate(SIZES.items()):
            frames=[]
            for i in range(240):
                art=frame(theme,kind,i/10);out=Image.new('RGB',art.size,BG);out.paste(art,mask=art.getchannel('A'))
                frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
            frames[0].save(root/f'{theme}-{kind}.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
            art=frame(theme,kind,3.5);art.save(root/f'{theme}-{kind}.png')
            d.text((20,15+j*230),kind.upper(),fill='#c8c5ae')
            art=art.resize((w*4,184),Image.Resampling.NEAREST);board.paste(art,(20,42+j*230),art)
            assert frame(theme,kind,0).tobytes()==frame(theme,kind,24).tobytes()
            print(theme,kind,(root/f'{theme}-{kind}.gif').stat().st_size)
        board.save(root/'contact-sheet.png')

if __name__=='__main__':main()
