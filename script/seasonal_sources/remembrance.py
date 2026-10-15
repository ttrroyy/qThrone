"""Code-native pixel animation studies: remembrance, captivity, freedom and care."""
from pathlib import Path
import math
from PIL import Image, ImageDraw

ROOT=Path(__file__).parent
SIZES={'wide':136,'compact':92,'narrow':48}
BG='#202020'
INK='#3e3428'

def ease(x):
    x=max(0,min(1,x));return x*x*(3-2*x)

def draw(kind,t):
    clock=t%30
    t=clock if clock<=15 else 30-clock
    w=SIZES[kind];c={'wide':104,'compact':66,'narrow':24}[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    lift=ease((t-5)/3)
    free=ease((t-8)/2)
    smile=t>=10
    def poly(points,col):d.polygon([(c+x,y) for x,y in points],fill=col)
    def rect(x,y,ww,hh,col):d.rectangle((c+x,y,c+x+ww-1,y+hh-1),fill=col)
    # Quiet wet stone and puddles keep all three sizes grounded.
    half={'wide':55,'compact':37,'narrow':23}[kind]
    d.polygon([(2,44),(9,42),(w//3,42),(w//2,43),(w-17,42),(w-4,43),(w-1,45),(2,45)],fill='#37464c')
    for x,y,ww in [(-20,44,9),(11,44,11),(-half+2,44,7)]:rect(x,y,ww,1,'#607b83')
    # Background rain eases after liberation; no flashing or strobe.
    rain_count={'wide':29,'compact':19,'narrow':11}[kind]
    for i in range(rain_count):
        x=round(((i*.61803398875)%1)*(w-4))+2
        y=round(((i*.41421356237+clock/30*(6+i%3))%1)*54)-6
        d.line((x,y,x-1,y+3),fill=tuple(round(a+(b-a)*free) for a,b in zip((81,100,109),(57,72,79))))
    # White carnations laid down on the left, with irregular layered petals.
    if kind!='narrow':
        blooms=[(48,33),(62,37)] if kind=='wide' else [(24,33),(35,38)]
        end=8 if kind=='wide' else 3
        for j,(fx,fy) in enumerate(blooms):
            d.line([(end+j*5,44-j),(fx-10,fy+5),(fx,fy+2)],fill='#6c8060')
            d.line([(end+j*5,45-j),(fx-1,fy+3)],fill='#425e4d')
            d.polygon([(fx-9,fy+5),(fx-17,fy+2),(fx-13,fy+7)],fill='#6c8060')
            d.polygon([(fx-16,fy+7),(fx-23,fy+11),(fx-12,fy+9)],fill='#4d6b52')
            # Serrated edges and cool-white creases distinguish carnations from daisies.
            pts=[(-5,-1),(-4,-3),(-2,-3),(-2,-5),(0,-4),(2,-5),(3,-3),(5,-3),(4,-1),(6,0),(4,2),(4,4),(1,3),(0,5),(-2,3),(-4,4),(-4,2),(-6,1)]
            d.polygon([(fx+x,fy+y) for x,y in pts],fill='#9cacae')
            pts=[(-4,-1),(-3,-3),(-1,-2),(0,-4),(2,-3),(2,-1),(4,-2),(3,0),(4,2),(2,2),(1,4),(-1,2),(-3,3),(-3,1),(-5,0)]
            d.polygon([(fx+x,fy+y) for x,y in pts],fill='#edf0e6')
            d.line([(fx-2,fy),(fx,fy-2),(fx+2,fy),(fx+1,fy+2)],fill='#b9c8c7')
            d.point((fx,fy),fill='#ffffff');d.point((fx-3,fy-1),fill='#ffffff')
    # Duck: rounded head, curled tuft, folded body and tiny feet.
    rect(-9,42,6,2,'#bd7b38');rect(4,42,6,2,'#bd7b38')
    poly([(-9,24),(-13,29),(-14,36),(-11,41),(-5,43),(8,43),(14,39),(15,33),(11,27),(8,24)],INK)
    poly([(-8,25),(-12,30),(-12,36),(-9,40),(-4,41),(8,41),(12,38),(13,33),(9,27)],'#edb735')
    poly([(-9,29),(-9,36),(-5,39),(6,39),(8,35),(5,29)],'#ffe082')
    poly([(-8,7),(-12,10),(-14,15),(-14,21),(-11,26),(-5,29),(6,29),(12,26),(15,21),(15,15),(12,10),(7,7)],INK)
    poly([(-7,8),(-11,11),(-12,16),(-12,21),(-9,25),(-4,27),(6,27),(11,24),(13,20),(13,15),(10,11),(6,9)],'#ffd455')
    poly([(-8,10),(-10,13),(-10,18),(-8,18),(-7,13),(-3,11)],'#ffedab')
    poly([(-3,8),(-2,5),(1,4),(4,5),(2,7),(5,8)],'#ffd455')
    rect(-1,5,2,1,'#ffedab')
    blink=(t%6)<.2
    if smile:
        if blink:
            rect(-7,17,4,1,'#443f39');rect(5,17,4,1,'#443f39')
        else:
            # Arched, clearly happy eyes.
            rect(-7,16,1,2,'#443f39');rect(-6,15,2,1,'#443f39');rect(-4,16,1,2,'#443f39')
            rect(5,16,1,2,'#443f39');rect(6,15,2,1,'#443f39');rect(8,16,1,2,'#443f39')
    else:
        if blink:
            rect(-7,18,4,1,'#443f39');rect(5,18,4,1,'#443f39')
        else:
            rect(-6,17,2,3,'#443f39');rect(6,17,2,3,'#443f39')
            rect(-6,17,1,1,'#fff0bc');rect(6,17,1,1,'#fff0bc')
        d.line((c-8,15,c-4,14),fill='#8a6b3d');d.line((c+5,14,c+9,15),fill='#8a6b3d')
        if t<5:
            tear=20+int((t%2)*2);rect(-7,tear,1,2,'#8bbac2')
    poly([(-3,20),(1,18),(6,20),(7,22),(3,24),(-2,23)],'#c98336')
    poly([(-2,20),(1,19),(5,20),(5,21),(-1,21)],'#f3aa42')
    if smile:
        d.line((c-1,22,c+3,23,c+5,22),fill='#875833')
        rect(-10,22,3,1,'#dc9a53');rect(9,22,3,1,'#dc9a53')
    else:rect(0,22,4,1,'#9c6536')
    # Wings travel inward to make a heart at the breast.
    if t<8:
        poly([(-10,29),(-7,30),(-5,34),(-6,37),(-10,35),(-12,32)],'#c99639')
        poly([(10,29),(7,30),(5,34),(6,37),(10,35),(12,32)],'#c99639')
        poly([(-10,29),(-8,30),(-6,34),(-7,35),(-10,33)],'#ffe087')
        poly([(10,29),(8,30),(6,34),(7,35),(10,33)],'#ffe087')
    else:
        poly([(-11,30),(-9,27),(-6,27),(-3,29),(0,32),(-1,36),(-5,35),(-10,33)],'#c99639')
        poly([(11,30),(9,27),(6,27),(3,29),(0,32),(1,36),(5,35),(10,33)],'#c99639')
        poly([(-10,30),(-8,28),(-6,28),(-3,30),(0,33),(-2,35),(-5,33),(-8,32)],'#ffe087')
        poly([(10,30),(8,28),(6,28),(3,30),(0,33),(2,35),(5,33),(8,32)],'#ffe087')
        # Small negative-space heart framed by the two wings.
        poly([(-4,29),(-2,29),(0,31),(2,29),(4,29),(5,31),(4,33),(0,36),(-4,33),(-5,31)],'#a66559')
        poly([(-3,30),(-2,30),(0,32),(2,30),(3,30),(4,31),(3,33),(0,35),(-3,33),(-4,31)],'#d99b88')
        rect(-3,30,1,1,'#efb9a3')
    # Open cuffs fall independently, leaving wrists visibly free.
    if t<10:
        drop=round(free*13);spread=round(free*5)
        for side in [-1,1]:
            x=c+side*(7+spread);y=34+drop
            if free<.15:
                d.rectangle((x-2,y-2,x+2,y+2),outline='#a6aeb0');d.point((x,y),fill='#4c6069')
            else:
                d.line([(x-2,y-2),(x-3,y),(x-1,y+2),(x+2,y+1)],fill='#a6aeb0',width=1)
        if free<.7:
            for j in range(5):
                x=c-4+j*2;y=36+drop+int(j%2)
                d.rectangle((x,y,x+1,y+1),outline='#8d9b9e')
    # Bars are foreground; lift the entire grate rather than fading through it.
    cage_half={'wide':24,'compact':22,'narrow':21}[kind]
    shift=round(lift*45)
    if shift<44:
        y0=2-shift;y1=42-shift
        metal=Image.new('RGBA',im.size);md=ImageDraw.Draw(metal)
        for x in range(-cage_half+4,cage_half,8):
            md.line((c+x,y0,c+x,y1),fill=(112,129,137,135))
        md.line((c-cage_half,y0,c+cage_half,y0),fill=(100,119,128,170))
        md.line((c-cage_half,40-shift,c+cage_half,40-shift),fill=(100,119,128,170))
        im=Image.alpha_composite(im,metal)
    return im

def main():
    for kind,w in SIZES.items():
        frames=[]
        for i in range(300):
            art=draw(kind,i/10);out=Image.new('RGB',art.size,BG)
            out.paste(art,mask=art.getchannel('A'));frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'remembrance-{kind}-v3.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2,optimize=False)
        print(kind,len(frames),(ROOT/f'remembrance-{kind}-v3.gif').stat().st_size)
    board=Image.new('RGB',(680,660),BG);d=ImageDraw.Draw(board)
    for j,t in enumerate([2.5,6.8,12]):
        d.text((15,j*215+10),['CAPTIVITY','BARS LIFT','FREE / HEART'][j],fill='#c4cbd0')
        art=draw('wide',t).resize((544,184),Image.Resampling.NEAREST)
        board.paste(art,(15,j*215+30),art)
    board.save(ROOT/'storyboard-v3.png')

if __name__=='__main__':main()
