from pathlib import Path
import sys,math,json
from PIL import Image,ImageDraw
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE))
import render
PAD=24
SHIFT_Y=0
class OffsetDraw:
    def __init__(self,draw):self.draw=draw
    def __getattr__(self,name):
        method=getattr(self.draw,name)
        if name not in ('polygon','rectangle','rounded_rectangle','ellipse','line','point','arc','text','textbbox'):return method
        def call(xy,*args,**kwargs):
            offset=PAD*render.SCALE
            if isinstance(xy[0],(int,float)):
                xy=tuple(v+offset+(SHIFT_Y*render.SCALE if i%2 else 0) for i,v in enumerate(xy))
            else:xy=[(x+offset,y+offset+SHIFT_Y*render.SCALE) for x,y in xy]
            result=method(xy,*args,**kwargs)
            return tuple(v-offset for v in result) if name=='textbbox' else result
        return call
class SafeCanvas(render.Canvas):
    def __init__(self,mode,size,color=None,raw=None):
        self.size=tuple(map(int,size));self.width,self.height=self.size
        self.raw=raw if raw is not None else Image.new(mode,((self.width+2*PAD)*render.SCALE,(self.height+2*PAD)*render.SCALE),color)
class SafeImages:
    new=staticmethod(lambda mode,size,color=None:SafeCanvas(mode,size,color))
    @staticmethod
    def alpha_composite(a,b):return SafeCanvas('RGBA',a.size,raw=Image.alpha_composite(a.raw,b.raw))
class SafeVectorDraw(render.VectorDraw):
    def __init__(self,image):self.image=image;self.d=OffsetDraw(ImageDraw.Draw(image.raw))
class SafeDraws:Draw=SafeVectorDraw


class module_floor:
    def __init__(self,d,width):self.d=d;self.width=width
    def __getattr__(self,name):
        method=getattr(self.d,name)
        def call(points,**kw):return method([(x*self.width/136,y) for x,y in points],**kw)
        return call

def load_safe(theme,variant):
    render.SCALE=6
    width={'wide':136,'compact':92,'narrow':48}[variant]
    module=render.load(theme);module.Image=SafeImages;module.ImageDraw=SafeDraws
    if theme=='halloween':
        original_ghost=module.ghost
        def safe_ghost(im,x,t):
            global SHIFT_Y
            phase=t%9
            peek=math.sin(phase*math.pi/5)**2 if phase<5 else 0
            old_y=(-18+peek*23) if t<9 else (46-peek*21)
            SHIFT_Y=7+3*math.sin(t*math.tau/18)-old_y
            try:original_ghost(im,x,t)
            finally:SHIFT_Y=0
        module.ghost=safe_ghost
        original_frame=module.frame
        def grounded_frame(kind,t):
            im=SafeImages.new('RGBA',(width,46));d=module_floor(SafeVectorDraw(im),width)
            d.polygon([(2,45),(7,43),(22,42),(37,43),(54,43),(67,44),(84,43),(100,44),(120,43),(133,45),
                (131,48),(115,49),(95,48),(76,49),(56,48),(35,49),(17,48),(4,47)],fill='#945c35')
            d.line([(7,44),(22,43),(38,44),(57,44),(76,45),(95,44),(116,44),(129,45)],fill='#bd7b43',width=.6)
            im.alpha_composite(original_frame(kind,t))
            return im
        module.frame=grounded_frame

    return module
