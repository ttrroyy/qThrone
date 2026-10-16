"""Responsive pixel-art studies based on SeasonalDecoration.hpp, not installed in app."""
from pathlib import Path
import math
from PIL import Image, ImageDraw

ROOT = Path(__file__).parent
INK = '#372536'
BG = '#202020'
VARIANTS = {'wide':136, 'compact':92, 'narrow':48}

def pumpkin(im,x,y,s,t,variant,hat=False,clear_face=False):
    d=ImageDraw.Draw(im)
    def pts(p): return [(round(x+a*s),round(y+b*s)) for a,b in p]
    def poly(p,c): d.polygon(pts(p), fill=c)
    def rect(a,b,w,h,c):
        a0,b0=pts([(a,b)])[0];a1,b1=pts([(a+w,b+h)])[0]
        d.rectangle((a0,b0,max(a0,a1-1),max(b0,b1-1)),fill=c)
    poly([(5,0),(10,-2),(16,0),(21,-1),(26,2),(29,7),(29,17),(25,22),(5,22),(1,18),(0,8),(2,3)],INK)
    poly([(6,1),(11,0),(16,2),(21,1),(25,3),(27,8),(27,16),(23,20),(6,20),(3,16),(2,8),(4,4)],'#d9772c')
    rect(8,2,4,17,'#ec983e');rect(19,3,3,16,'#be5b26');rect(5,5,2,10,'#f2ac51')
    poly([(12,1),(12,-5),(15,-7),(19,-7),(19,-5),(16,-5),(15,1)],'#66704a')
    rect(14,-5,1,4,'#92a165')
    blink=(t+variant*1.7)%6 < .2
    if clear_face:
        # Draw small faces directly on the final pixel grid: empty orange rows
        # separate eyes, nose and mouth even after rounding the pumpkin scale.
        def pixel_rect(a,b,w,h,c):
            d.rectangle((x+a,y+b,x+a+w-1,y+b+h-1),fill=c)
        if blink:
            pixel_rect(4,5,4,1,INK);pixel_rect(12,5,4,1,INK)
        else:
            pixel_rect(4,4,3,2,INK);pixel_rect(13,4,3,2,INK)
            pixel_rect(5,5,1,1,'#ffe89a');pixel_rect(14,5,1,1,'#ffe89a')
        pixel_rect(10,7,1,1,INK)
        # Keep the mouth below a full uncut row; retain a changing grin.
        pixel_rect(5,10,10,2,INK)
        if int(t/3+variant)%3 != 0:
            pixel_rect(4,9,1,2,INK);pixel_rect(15,9,1,2,INK)
            pixel_rect(7,10,1,1,'#ffe89a');pixel_rect(12,10,1,1,'#ffe89a')
        else:pixel_rect(9,10,2,1,'#ffe89a')
    else:
        if blink:
            rect(6,8,6,2,INK);rect(18,8,6,2,INK)
        else:
            poly([(6,6),(12,9),(11,12),(6,11)],INK)
            poly([(23,6),(18,9),(19,12),(24,11)],INK)
            rect(8,9,3,1,'#ffe89a');rect(20,9,2,1,'#ffe89a')
        poly([(14,11),(12,14),(16,14)],INK)
        if int(t/3+variant)%3 != 0:
            poly([(5,14),(9,16),(12,15),(15,17),(18,15),(22,16),(25,14),(23,19),(7,19)],INK)
            rect(9,16,2,1,'#ffe89a');rect(19,16,2,1,'#ffe89a')
        else:
            rect(10,16,10,3,INK);rect(13,16,2,1,'#ffe89a')
    if hat:
        # Bent witch hat, muted violet cloth, orange band and brass buckle.
        poly([(-3,1),(3,-3),(7,-4),(10,-16),(18,-20),(24,-17),(18,-16),(21,-5),(26,-2),(31,0),(30,3),(-2,3)],INK)
        poly([(1,0),(8,-2),(12,-15),(18,-18),(21,-17),(15,-16),(18,-5),(24,-1),(28,1),(1,1)],'#5f4775')
        poly([(12,-14),(16,-16),(13,-6),(10,-3),(8,-3)],'#806092')
        poly([(8,-4),(19,-5),(21,-2),(7,-1)],'#c87937')
        rect(13,-5,5,4,'#e5bb68');rect(14,-4,3,2,INK)

def ghost(im,x,t):
    # Original alternating top/bottom peek, with periods aligned to the GIF loop.
    phase=t%9
    peek=math.sin(phase*math.pi/5)**2 if phase<5 else 0
    y=round(-18+peek*23) if t<9 else round(46-peek*21)
    d=ImageDraw.Draw(im)
    def poly(p,c): d.polygon([(x+a,y+b) for a,b in p],fill=c)
    poly([(4,0),(11,0),(15,3),(16,8),(16,18),(13,16),(10,18),(7,16),(4,18),(0,16),(0,7),(1,3)],INK)
    poly([(4,1),(10,1),(14,4),(15,8),(15,15),(12,14),(10,16),(7,14),(4,16),(1,14),(1,7),(2,4)],'#d9e5e7')
    for a,b,w,h,c in [(2,6,3,7,'#a3cbd1'),(5,5,3,4,'#203349'),(10,5,3,4,'#203349'),(6,5,1,1,'#ffffff'),(11,5,1,1,'#ffffff'),(8,10,2,2,'#486c7b')]:
        d.rectangle((x+a,y+b,x+a+w-1,y+b+h-1),fill=c)

def web(im,right,t,small=False):
    d=ImageDraw.Draw(im)
    radius=20 if small else 34
    for dx,dy in [(radius,0),(radius-7,17),(10,24),(0,27)]:
        d.line([(right,0),(right-dx,dy)],fill='#625769')
    for r in ([8,16] if small else [8,16,25,34]):
        d.line([(right-r,0),(right-r+3,r//3),(right-r//2,r//2),(right-r//5,r*2//3),(right,r*3//4)],fill='#625769')
    x=right-12;y=12+round(6*(1-math.cos(t*2*math.pi/18)))
    d.line((x,5,x,y),fill='#9a899e')
    leg=int(t*2)%2
    for side in [-1,1]:
        for oy in [-1,1,3]:
            d.line([(x+side*2,y+oy),(x+side*5,y+oy-2+leg),(x+side*6,y+oy+1)],fill='#937a9e')
    d.rectangle((x-2,y-1,x+2,y+3),fill='#937a9e')
    for ex in [-1,1]:d.point((x+ex,y),fill='#ded7e7')

def leaf(im,x,y,s,angle,color):
    a=math.radians(angle);c=math.cos(a);sn=math.sin(a)
    def p(px,py): return round(x+s*(px*c-py*sn)),round(y+s*(px*sn+py*c))
    d=ImageDraw.Draw(im)
    d.polygon([p(*v) for v in [(0,0),(3,-1),(2,-4),(5,-3),(6,-6),(8,-3),(11,-4),(10,-1),(13,0),(9,3),(6,4),(2,3)]],fill=color)
    d.line([p(6,-2),p(6,4)],fill='#e6a05c')

def candle(im,x,t):
    d=ImageDraw.Draw(im);f=int(t*5)%3
    d.rectangle((x,34,x+6,44),fill=INK);d.rectangle((x+1,34,x+5,43),fill='#e8d3a3')
    d.rectangle((x+1,35,x+2,42),fill='#fff0c4');d.rectangle((x+4,35,x+4,38),fill='#c5a777')
    d.rectangle((x,44,x+7,45),fill='#725348');d.line((x+3,31,x+3,34),fill=INK)
    d.polygon([(x+3,24-f),(x+6,29),(x+5,32),(x+1,32),(x,29)],fill='#dc8738')
    d.polygon([(x+3,27),(x+5,30),(x+3,32),(x+2,30)],fill='#ffe6a0')

def frame(kind,t):
    w=VARIANTS[kind];im=Image.new('RGBA',(w,46))
    if kind=='wide':
        web(im,135,t);ghost(im,85,t)
        for x,y,s,a,c in [(3,43,.72,-28,'#955132'),(10,39,.8,22,'#b76736'),(14,45,.86,-12,'#cd8240'),(81,43,.7,-20,'#ad6234'),(115,40,.76,25,'#955132'),(110,45,.88,-16,'#cd8240'),(124,44,.62,-48,'#b76736')]:leaf(im,x,y,s,a,c)
        pumpkin(im,14,29,.76,t,0,True);pumpkin(im,36,21,1.12,t,1);pumpkin(im,66,32,.58,t,2)
        candle(im,97,t);leaf(im,77,45,.54,-14,'#b76736');leaf(im,91,45,.52,22,'#cd8240')
    elif kind=='compact':
        web(im,91,t,True);ghost(im,46,t)
        leaf(im,0,43,.7,-25,'#955132');leaf(im,9,45,.7,15,'#cd8240');leaf(im,77,44,.7,-20,'#b76736')
        pumpkin(im,9,25,.92,t,0,True)
        # Copy the wide composition's exact small pumpkin before translating it.
        small=Image.new('RGBA',(136,46))
        pumpkin(small,66,32,.58,t,2)
        if hasattr(small,'raw'):
            scale=getattr(small,'drawing_scale',small.raw.width/small.width)
            guard_x=(small.raw.width-small.width*scale)/2
            guard_y=(small.raw.height-small.height*scale)/2
            box=(int(guard_x+65*scale),int(guard_y),int(guard_x+85*scale),int(guard_y+46*scale))
            sprite=small.raw.crop(box)
            im.raw.alpha_composite(sprite,(int(guard_x+34*scale),int(guard_y)))
        else:
            im.alpha_composite(small.crop((65,0,85,46)),(34,0))
        candle(im,65,t);leaf(im,54,45,.55,20,'#cd8240')
    else:
        # One full-height character; remove scenery instead of shrinking everything.
        web(im,47,t,True)
        leaf(im,0,44,.63,-18,'#b76736');leaf(im,33,44,.6,16,'#cd8240')
        pumpkin(im,7,25,.92,t,0,True)
    return im

def main():
    for kind in VARIANTS:
        still=frame(kind,2.2)
        still.save(ROOT/f'halloween-{kind}.png')
        still.resize((still.width*4,184),Image.Resampling.NEAREST).save(ROOT/f'halloween-{kind}-preview.png')
        frames=[]
        for i in range(180):
            art=frame(kind,i/10)
            canvas=Image.new('RGB',art.size,BG);canvas.paste(art,mask=art.getchannel('A'))
            frames.append(canvas.resize((canvas.width*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'halloween-{kind}.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
    board=Image.new('RGB',(680,710),BG);d=ImageDraw.Draw(board)
    for i,(kind,w) in enumerate(VARIANTS.items()):
        y=15+i*230;d.text((20,y),f'{kind.upper()}  {w} x 46  |  4x preview',fill='#ded7e7')
        art=frame(kind,2.2).resize((w*4,184),Image.Resampling.NEAREST)
        board.paste(art,(20,y+27),art)
        d.line((20,y+212,640,y+212),fill='#555555')
    board.save(ROOT/'contact-sheet.png')
    for path in sorted(ROOT.glob('*')):
        if path.suffix in {'.png','.gif'}:print(path.name,path.stat().st_size)

if __name__=='__main__':main()
