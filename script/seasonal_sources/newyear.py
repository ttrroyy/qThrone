"""Render native qThrone New Year artwork as three responsive animation studies."""
from pathlib import Path
import math
import re
from PIL import Image, ImageDraw

ROOT=Path(__file__).parent
SOURCE=ROOT/'original_decoration.hpp'
CPP=SOURCE.read_text(encoding='utf-8')
BODY=CPP.split('static void paintNewYear',1)[1].split('void updateBounds',1)[0]
TREE=BODY.split('rect(36,34',1)[1].split('for (int i=0;i<7',1)[0]
TREE='rect(36,34'+TREE
MAN=BODY.split('// The snowman blinks;',1)[1].split('// Sparse flakes',1)[0]
SNOW=BODY.split('// Low snow banks',1)[1].split('rect(36,34',1)[0]
GIFT=MAN.split('rect(105,34',1)[1]
GIFT='rect(105,34'+GIFT
MAN=MAN.split('rect(105,34',1)[0]
COLORS={'outline':'#385361','snow':'#e5f2f3','shade':'#a8ccd5'}
SIZES={'wide':136,'compact':92,'narrow':48}

def primitives(im,source,dx=0,t=0):
    d=ImageDraw.Draw(im)
    pattern=r'poly\(\{(.*?)\},\s*("#[0-9a-fA-F]+"|outline|snow|shade)\s*\)|rect\(([^;]+)\);'
    for m in re.finditer(pattern,source,re.S):
        if m.group(1) is not None:
            points=[(int(a)+dx,int(b)) for a,b in re.findall(r'\{(-?\d+),(-?\d+)\}',m.group(1))]
            color=m.group(2).strip('"');color=COLORS.get(color,color)
            if points:d.polygon(points,fill=color)
        else:
            args=m.group(3).split(',')
            if len(args)!=5:continue
            nums=[]
            for value in args[:4]:
                value=value.strip()
                if value=='blink?1:2':nums.append(1 if t%6<.22 else 2)
                elif re.fullmatch(r'-?\d+',value):nums.append(int(value))
                else:break
            if len(nums)!=4:continue
            x,y,w,h=nums;color=args[4].strip().strip('"');color=COLORS.get(color,color)
            d.rectangle((x+dx,y,x+dx+w-1,y+h-1),fill=color)

def tree(im,dx,t):
    primitives(im,TREE,dx,t)
    d=ImageDraw.Draw(im)
    for i,(x,y) in enumerate([(37,15),(43,20),(34,24),(46,27),(29,34),(39,34),(51,35)]):
        color='#fff0b4' if (int(t*1.5)+i)%4==0 else ['#e88672','#edc977','#a0c4df'][i%3]
        d.rectangle((x+dx,y,x+dx+1,y+1),fill=color)

def snowman(im,dx,t):
    primitives(im,MAN,dx,t)
    d=ImageDraw.Draw(im)
    for a,b,c,e in [(70,32,64,28),(64,28,63,25),(93,32,100,27),(100,27,103,25),(100,27,100,23)]:
        d.line((a+dx,b,c+dx,e),fill='#977359')

def frame(kind,t):
    w=SIZES[kind];im=Image.new('RGBA',(w,46));d=ImageDraw.Draw(im)
    if kind=='wide':primitives(im,SNOW)
    else:
        # Same snow height, rebuilt to match the composition's actual footprint.
        d.polygon([(1,44),(6,42),(w//3,41),(w//2,42),(w*2//3,40),(w-6,42),(w-1,44),(w-1,45),(1,45)],fill=COLORS['outline'])
        d.polygon([(2,44),(7,43),(w//3,42),(w//2,43),(w*2//3,41),(w-7,43),(w-2,44),(w-2,45),(2,45)],fill=COLORS['snow'])
    if kind=='wide':
        tree(im,0,t);snowman(im,0,t);primitives(im,GIFT)
    elif kind=='compact':
        tree(im,-14,t);snowman(im,-17,t)
    else:snowman(im,-59,t)
    d=ImageDraw.Draw(im)
    count={'wide':17,'compact':11,'narrow':6}[kind]
    for i in range(count):
        seed=((i+1)*.61803398875)%1
        phase=((i+1)*.41421356237)%1
        # A seamless 24-second loop; snow stays local to the decoration.
        x=round(3+seed*(w-6)+1.8*math.sin(t*2*math.pi/24+i*2.41))
        y=round(((t/24*(1+i%2)+phase)%1)*51-3)
        d.point((x,y),fill='#afcbd5')
        if i%5==0:
            d.line((x-1,y,x+1,y),fill='#afcbd5');d.line((x,y-1,x,y+1),fill='#afcbd5')
    return im

def main():
    board=Image.new('RGB',(680,710),'#202020');d=ImageDraw.Draw(board)
    for j,(kind,w) in enumerate(SIZES.items()):
        still=frame(kind,2.5);still.save(ROOT/f'newyear-{kind}.png')
        enlarged=still.resize((w*4,184),Image.Resampling.NEAREST)
        enlarged.save(ROOT/f'newyear-{kind}-preview.png')
        d.text((20,15+j*230),f'{kind.upper()}  {w} x 46',fill='#ded7e7')
        board.paste(enlarged,(20,42+j*230),enlarged)
        d.line((20,227+j*230,640,227+j*230),fill='#555555')
        frames=[]
        for i in range(240):
            art=frame(kind,i/10);out=Image.new('RGB',art.size,'#202020')
            out.paste(art,mask=art.getchannel('A'))
            frames.append(out.resize((w*4,184),Image.Resampling.NEAREST))
        frames[0].save(ROOT/f'newyear-{kind}.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
        print(kind,w,46,(ROOT/f'newyear-{kind}.gif').stat().st_size)
    board.save(ROOT/'contact-sheet.png')

if __name__=='__main__':main()
