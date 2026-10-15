"""Dense, sober May remembrance study: vertical years, pulsing red, black-centred poppies."""
from pathlib import Path
import math, random
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).parent
SIZES={'wide':544,'compact':368,'narrow':192}
BG='#202020'
FONT='C:/Windows/Fonts/consola.ttf'

def smooth(v):
    v=max(0,min(1,v));return v*v*(3-2*v)

def poppy(im,x,y,r,t,seed,retract=False,back=False):
    d=ImageDraw.Draw(im)
    phase=t*math.pi/6
    breath=.5+.5*math.sin(phase+seed*.27)
    scale=(.15+.85*smooth(breath)) if retract else (.86+.14*breath)
    r=max(1,r*scale)
    # Broad irregular poppy petals: shaded cups, creased rims and fine stamens.
    # A short green stem is visible only in gaps between the overlapping heads.
    stem=round(r*.30)
    lean=round(math.sin(seed*1.3)*r*.16)
    d.line([(round(x+lean),round(y+r+stem)),(round(x+lean*.4),round(y+r*.6))],fill='#426444',width=2)
    if r>17:
        d.line([(round(x+lean),round(y+r*.9)),(round(x+lean-3),round(y+r*.78))],fill='#607b50')
    angle=seed*.91
    for j in range(5):
        a=angle+j*math.tau/5
        base=[(133,21,30),(170,27,33),(191,33,37),(158,20,29),(183,29,33)][j]
        for layer in range(4):
            pts=[]
            for k in range(64):
                q=k*math.tau/64
                fold=1+.025*math.sin(q*7+seed+j)+.012*math.sin(q*13+j)
                along=r*(.20+(.80-layer*.065)*math.cos(q))*fold
                across=r*(.73-layer*.045)*math.sin(q)*fold
                pts.append((round(x+math.cos(a)*along-math.sin(a)*across),round(y+math.sin(a)*along+math.cos(a)*across)))
            color=tuple(min(255,v+layer*9) for v in base)
            d.polygon(pts,fill=color)
        for k in range(15):
            aa=a+(k-7)*.075
            length=r*(.66+.15*math.sin(k*2.4+seed+j))
            mid=r*.44
            dark=(105+j*6,17,26) if k%3 else (217,53,49)
            d.line([(round(x+math.cos(aa)*r*.23),round(y+math.sin(aa)*r*.23)),(round(x+math.cos(aa+.025)*mid),round(y+math.sin(aa+.025)*mid)),(round(x+math.cos(aa)*length),round(y+math.sin(aa)*length))],fill=dark)
    rr=max(2,round(r*.26))
    d.ellipse((round(x-rr-2),round(y-rr-2),round(x+rr+2),round(y+rr+2)),fill='#29191c')
    for j in range(32):
        a=j*math.tau/32
        length=rr+3+(j%3)
        d.line((round(x+math.cos(a)*rr*.7),round(y+math.sin(a)*rr*.7),round(x+math.cos(a)*length),round(y+math.sin(a)*length)),fill='#161717')
    d.ellipse((round(x-rr),round(y-rr),round(x+rr),round(y+rr)),fill='#151616')
    if rr>3:
        for j in range(6):
            a=j*math.tau/6
            d.line((x,y,round(x+math.cos(a)*(rr-2)),round(y+math.sin(a)*(rr-2))),fill='#2b2928')

def candle(im,x,t,small=False):
    d=ImageDraw.Draw(im);width=17 if small else 32
    y=74 if small else 53
    d.rectangle((x-2,y-2,x+width+1,170),fill='#584c40')
    d.rectangle((x,y,x+width-1,168),fill='#c6b799')
    d.rectangle((x+2,y,x+4,166),fill='#e1d4b6')
    d.line((x+width-3,y+2,x+width-3,165),fill='#9d8f77')
    d.ellipse((x,y-3,x+width-1,y+3),fill='#dbceb0')
    d.line((x+width-5,y+2,x+width-5,y+14,x+width-7,y+18),fill='#dfd1ad',width=2)
    cx=x+width//2;tip=y-26+round(math.sin(t*math.pi/1.5)*1.4)
    d.line((cx,y-5,cx,y),fill='#39302a')
    d.polygon([(cx,tip),(cx+5,y-13),(cx+4,y-6),(cx,y-3),(cx-4,y-7),(cx-5,y-12)],fill='#b2854f')
    d.polygon([(cx,tip+7),(cx+2,y-11),(cx,y-5),(cx-2,y-8),(cx-1,y-15)],fill='#e5cf94')

def calendar(im,x,width,t):
    d=ImageDraw.Draw(im);right=x+width-1
    # A compact, tall sheet rather than the previous wide desk-calendar board.
    d.polygon([(x+2,6),(right-3,4),(right,171),(x,173)],fill='#8b8375')
    d.polygon([(x+3,8),(right-5,7),(right-2,167),(x+3,169)],fill='#c0b7a3')
    d.rounded_rectangle((x+1,6,right-3,20),radius=2,fill='#785b55')
    d.line((x+4,7,right-6,7),fill='#9a7c6d')
    d.line((x+3,19,right-5,19),fill='#503f3b')
    for bx in [x+11,right-14]:
        d.rounded_rectangle((bx-1,1,bx+4,15),radius=1,fill='#454446')
        d.rectangle((bx,2,bx+1,12),fill='#aaa49b')
    font=ImageFont.truetype(FONT,9)
    left=x+6;slot=(width-13)/7
    for k in range(7):
        xx=round(left+k*slot)
        d.text((xx,21),str(39+k),font=font,fill='#514b42')
        for m in range(12):
            yy=37+m*10
            for j in range(4):d.point((xx+j*2,yy),fill='#958b79')
    # Sept 1939 -> May 1945, with full-height intervening years.
    # Broad near-touching stains breathe as one, rather than dots running down them.
    global_breath=.5+.5*math.sin(t*math.pi/6)
    for k in range(7):
        mid=left+(k+.5)*slot
        start=37+8*10 if k==0 else 37
        end=37+4*10 if k==6 else 159
        half=slot*(.40+.035*math.sin(t*math.pi/6+k*.12))
        tint=.7*global_breath+.3*(.5+.5*math.sin(t*math.pi/6+k*.12))
        color=tuple(round(a+(b-a)*tint) for a,b in zip((66,18,32),(113,26,43)))
        edge=[]
        for yy in range(start,end+1,3):
            wobble=math.sin(yy*.10+k)*.8
            edge.append((round(mid-half+wobble),yy))
        for yy in range(end,start-1,-3):
            wobble=math.sin(yy*.13+k+.7)*.8
            edge.append((round(mid+half+wobble),yy))
        d.polygon(edge,fill=color)
        # Deep burgundy centre with slow spatial sheen, no travelling droplets.
        for yy in range(start,end+1):
            for xx in range(round(mid-half+1),round(mid+half)):
                core=max(0,1-abs(xx-mid)/max(1,half))
                middle=math.sin((yy-start)/max(1,end-start)*math.pi)
                sheen=.5+.5*math.sin(t*math.pi/6+yy*.022+k*.18)
                shade=1-.31*core-.15*middle+.10*sheen
                d.point((xx,yy),fill=tuple(round(v*shade) for v in color))
        # Rounded heavy end; length pulses a couple of pixels without travelling beads.
        bottom=end+round(2*math.sin(t*math.pi/6+k*.12))
        d.ellipse((round(mid-half),bottom-6,round(mid+half),bottom+2),fill=color)
    return left,slot

def frame(kind,t):
    w=SIZES[kind];im=Image.new('RGBA',(w,184))
    calwidth={'wide':198,'compact':160,'narrow':118}[kind]
    calx={'wide':292,'compact':174,'narrow':63}[kind]
    # A low rounded mound, not a full-height flower wallpaper.
    # Highest near the centre, gently sloping down to the lower corners.
    rng=random.Random(71)
    step={'wide':46,'compact':39,'narrow':29}[kind]
    seed=0
    for row,y in enumerate([122,151,181]):
        for x in range(12-row%2*12,w+10,step):
            dx=(x-w*.45)/(w*.57)
            crest=112+39*dx*dx
            if y<crest:continue
            xx=x+rng.randint(-6,6);yy=y+rng.randint(-4,4)
            rr=rng.randint(26,37) if kind=='wide' else rng.randint(21,30) if kind=='compact' else rng.randint(16,22)
            poppy(im,xx,yy,rr,t,seed,back=True);seed+=1
    left,slot=calendar(im,calx,calwidth,t)
    candlex={'wide':155,'compact':77,'narrow':18}[kind]
    candle(im,candlex,t,kind=='narrow')
    # Blossoms open from the stained sheet itself and withdraw into the edges.
    # They overlap both sides of the blood bands, without hiding year headings.
    sheetflowers=[(calx+7,121,23),(calx+calwidth-11,143,24),(calx+calwidth*.44,160,23),(calx+calwidth*.31,137,18),(calx+calwidth-8,89,11)]
    for i,(x,y,r) in enumerate(sheetflowers):
        poppy(im,round(x),y,r,t,i+20,retract=True)
    # Foreground grows across the candle wax and over the paper's lower edge.
    for i,x in enumerate(range(14,w+10,40 if kind!='narrow' else 27)):
        poppy(im,x,171+(i%2)*5,28 if kind=='wide' else 24 if kind=='compact' else 18,t,80+i)
    for i,(x,y,r) in enumerate([(candlex-4,127,21),(candlex+30,153,27),(candlex-9,169,23)]):
        poppy(im,x,y,r,t,120+i,retract=True)
    return im

def main():
    board=Image.new('RGB',(584,690),BG);d=ImageDraw.Draw(board)
    for j,(kind,w) in enumerate(SIZES.items()):
        frames=[]
        for i in range(120):
            art=frame(kind,i/5);out=Image.new('RGB',art.size,BG);out.paste(art,mask=art.getchannel('A'));frames.append(out)
        frames[0].save(ROOT/f'may-remembrance-{kind}-v7.gif',save_all=True,append_images=frames[1:],duration=200,loop=0,optimize=False,disposal=2)
        art=frame(kind,2.5);art.save(ROOT/f'may-{kind}-v7.png')
        d.text((15,15+j*225),kind.upper(),fill='#c7c2b9');board.paste(art,(15,38+j*225),art)
        print(kind,(ROOT/f'may-remembrance-{kind}-v7.gif').stat().st_size)
    board.save(ROOT/'contact-sheet-v7.png')

if __name__=='__main__':main()
