"""Approved high-resolution seasonal artwork renderer."""
from pathlib import Path
import ast, math, types, json, zipfile
from PIL import Image as PILImage, ImageDraw as PILDraw, ImageFont, ImageChops, ImageStat

BASE = Path(__file__).parent
SOURCE = BASE.parent / 'seasonal_sources'
OUT = BASE / 'animations'
SCALE = 6
BG = '#202020'
THEMES = {'halloween':18, 'newyear':24, 'remembrance':30, 'may_remembrance':24,
          'kupala':24, 'patrick':24, 'valentine':24, 'maslenitsa':24, 'christmas':24, 'easter':24,
          'spring':24, 'summer':24, 'autumn':24, 'winter':24}

class Coordinate(float):
    def __index__(self): return int(round(float(self)))

def precise(value, ndigits=None):
    return Coordinate(value if ndigits is None else round(value,ndigits))

def colour(c):
    return tuple(max(0,min(255,round(x))) for x in c) if isinstance(c,tuple) else c

def points(xy):
    return list(zip(xy[::2],xy[1::2])) if xy and isinstance(xy[0],(int,float)) else list(xy)

class Canvas:
    def __init__(self,mode,size,color=None,raw=None):
        self.size=tuple(map(int,size));self.width,self.height=self.size
        self.raw=raw if raw is not None else PILImage.new(mode,(self.width*SCALE,self.height*SCALE),color)
    def crop(self,box):
        return Canvas(self.raw.mode,(box[2]-box[0],box[3]-box[1]),raw=self.raw.crop(tuple(round(x*SCALE) for x in box)))
    def alpha_composite(self,other,dest=(0,0)):
        self.raw.alpha_composite(other.raw,tuple(round(x*SCALE) for x in dest))

class Images:
    new=staticmethod(lambda mode,size,color=None:Canvas(mode,size,color))
    @staticmethod
    def alpha_composite(a,b): return Canvas('RGBA',a.size,raw=PILImage.alpha_composite(a.raw,b.raw))

class VectorDraw:
    def __init__(self,image): self.image=image;self.d=PILDraw.Draw(image.raw)
    def polygon(self,xy,fill=None,outline=None,width=1):
        p=points(xy)
        if len(p)<3:return
        if SCALE==2:
            # May petals already contain detailed curved 64-point outlines.
            self.d.polygon([(x*SCALE,y*SCALE) for x,y in p],fill=colour(fill),outline=colour(outline),width=max(1,round(width*SCALE)))
            return
        # Rounded vector corners rather than enlarged staircase pixels.
        result=[]
        for i,b in enumerate(p):
            a=p[i-1];c=p[(i+1)%len(p)]
            ab=math.dist(a,b);bc=math.dist(b,c)
            radius=min(1.5,ab*.29,bc*.29)
            if not ab or not bc:continue
            q=(b[0]+(a[0]-b[0])*radius/ab,b[1]+(a[1]-b[1])*radius/ab)
            r=(b[0]+(c[0]-b[0])*radius/bc,b[1]+(c[1]-b[1])*radius/bc)
            for j in range(7):
                t=j/6;u=1-t
                result.append(((u*u*q[0]+2*u*t*b[0]+t*t*r[0])*SCALE,
                               (u*u*q[1]+2*u*t*b[1]+t*t*r[1])*SCALE))
        if result:self.d.polygon(result,fill=colour(fill),outline=colour(outline),width=max(1,round(width*SCALE)))
    def rectangle(self,box,fill=None,outline=None,width=1):
        x0,y0,x1,y1=box;x1=max(x0+.25,x1+1);y1=max(y0+.25,y1+1)
        radius=min(.70,(x1-x0)*.25,(y1-y0)*.25)*SCALE
        self.d.rounded_rectangle((x0*SCALE,y0*SCALE,x1*SCALE-.2,y1*SCALE-.2),radius=radius,
                                fill=colour(fill),outline=colour(outline),width=max(1,round(width*SCALE)))
    def ellipse(self,box,fill=None,outline=None,width=1):
        self.d.ellipse(tuple(x*SCALE for x in box),fill=colour(fill),outline=colour(outline),width=max(1,round(width*SCALE)))
    def square(self,box,fill=None):
        self.d.rectangle(tuple(x*SCALE for x in box),fill=colour(fill))
    def rounded_rectangle(self,box,radius=1,fill=None,outline=None,width=1):
        self.d.rounded_rectangle(tuple(x*SCALE for x in box),radius=radius*SCALE,fill=colour(fill),outline=colour(outline),width=max(1,round(width*SCALE)))
    def line(self,xy,fill=None,width=1,joint=None):
        p=[(x*SCALE,y*SCALE) for x,y in points(xy)]
        stroke=max(1,round(width*SCALE*.78))
        self.d.line(p,fill=colour(fill),width=stroke,joint='curve')
        r=stroke/2
        for x,y in p:self.d.ellipse((x-r,y-r,x+r,y+r),fill=colour(fill))
    def point(self,xy,fill=None):
        x,y=xy;r=.32*SCALE
        self.d.ellipse((x*SCALE-r,y*SCALE-r,x*SCALE+r,y*SCALE+r),fill=colour(fill))
    def arc(self,box,start,end,fill=None,width=1):
        self.d.arc(tuple(x*SCALE for x in box),start,end,fill=colour(fill),width=max(1,round(width*SCALE*.8)))
    def text(self,xy,text,fill=None,font=None,**kwargs):
        if font and hasattr(font,'path'):font=ImageFont.truetype(font.path,round(font.size*SCALE))
        self.d.text(tuple(x*SCALE for x in xy),text,fill=colour(fill),font=font,**kwargs)
    def textbbox(self,xy,text,font=None,**kwargs):
        if font and hasattr(font,'path'):font=ImageFont.truetype(font.path,round(font.size*SCALE))
        box=self.d.textbbox(tuple(x*SCALE for x in xy),text,font=font,**kwargs)
        return tuple(x/SCALE for x in box)

class Draws:
    Draw=VectorDraw

class Rounds(ast.NodeTransformer):
    def visit_Call(self,node):
        self.generic_visit(node)
        if isinstance(node.func,ast.Name) and node.func.id=='round':node.func.id='precise'
        return node

def load(theme):
    name='christmas_easter' if theme in ('christmas','easter') else theme
    if theme in ('spring','summer','autumn','winter'):name='seasons'
    path=SOURCE/(name+'.py')
    if (BASE/'sources'/(name+'.py')).exists():path=BASE/'sources'/(name+'.py')
    tree=ast.parse(path.read_text(encoding='utf-8'))
    # Retain exact geometry, timing and layering; redraw primitives at high resolution.
    if theme!='may_remembrance':tree=Rounds().visit(tree)
    module=types.ModuleType(name);module.__file__=str(path)
    exec(compile(ast.fix_missing_locations(tree),str(path),'exec'),module.__dict__)
    module.Image=Images;module.ImageDraw=Draws;module.precise=precise
    return module

def render(module,theme,variant,t):
    global SCALE
    SCALE=2 if theme=='may_remembrance' else 6
    if theme in ('christmas','easter','spring','summer','autumn','winter'):art=module.frame(theme,variant,t)
    elif theme=='remembrance':art=module.draw(variant,t)
    else:art=module.frame(variant,t)
    target=(544 if variant=='wide' else 368,184)
    return art.raw.resize(target,PILImage.Resampling.LANCZOS)

def preview():
    OUT.mkdir(parents=True,exist_ok=True)
    board=PILImage.new('RGB',(1100,10*245+30),BG);d=PILDraw.Draw(board)
    for row,theme in enumerate(THEMES):
        module=load(theme)
        for variant,x in [('wide',15),('compact',610)]:
            art=render(module,theme,variant,2.5)
            art.save(OUT/f'{theme}-{variant}.png')
            d.text((x,15+row*245),f'{theme} / {variant}',fill='#ddd6ce')
            board.paste(art,(x,42+row*245),art)
        print(theme,flush=True)
    board.save(BASE/'all-themes-preview.jpg',quality=95)

if __name__=='__main__':preview()
