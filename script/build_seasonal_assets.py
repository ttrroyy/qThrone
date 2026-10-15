"""Bake approved smooth artwork into transparent atlases; runtime needs no Pillow."""
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor
import sys,json
from PIL import Image
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'script/seasonal_hd'))
from render import load,render
from mobile_safe import load_safe
DEST=ROOT/'res/seasonal'
THEMES={'halloween':18,'newyear':24,'easter':24,'kupala':24}
def bake(theme, variants=('wide','compact','narrow')):
    result={};count=THEMES[theme]*20
    for variant in variants:
        module=load_safe(theme,variant)
        def raw_frame(t):return (module.frame(theme,variant,t) if theme=='easter' else module.frame(variant,t)).raw
        # Twice the logical resolution bounds active atlas memory to about 48 MiB.
        w,h={'wide':272,'compact':184,'narrow':96}[variant],92
        bounds=None
        for index in range(count):
            image=raw_frame(index/20);box=image.getchannel('A').getbbox()
            assert box and box[0]>0 and box[1]>0 and box[2]<image.width and box[3]<image.height
            bounds=box if bounds is None else (min(bounds[0],box[0]),min(bounds[1],box[1]),max(bounds[2],box[2]),max(bounds[3],box[3]))
        ratio=min((w-4)/(bounds[2]-bounds[0]),(h-4)/(bounds[3]-bounds[1]))
        size=(round((bounds[2]-bounds[0])*ratio),round((bounds[3]-bounds[1])*ratio))
        columns=8
        atlas=Image.new('RGBA',(w*columns,h*((count+columns-1)//columns)))
        for index in range(count):
            content=raw_frame(index/20).crop(bounds).resize(size,Image.Resampling.LANCZOS)
            art=Image.new('RGBA',(w,h));art.alpha_composite(content,((w-size[0])//2,h-2-size[1]))
            atlas.paste(art,((index%columns)*w,(index//columns)*h))
        filename=f'{theme}-{variant}.png'
        atlas.save(DEST/filename,compress_level=9)
        result[variant]={'file':filename,'width':w,'height':h,'columns':columns,'frames':count,'interval':50}
        print(filename,(DEST/filename).stat().st_size,flush=True)
    return theme,result
def main():
    DEST.mkdir(parents=True,exist_ok=True)
    with ProcessPoolExecutor(max_workers=2) as pool:manifest=dict(pool.map(bake,THEMES))
    (DEST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    write_resource_indexes(manifest)

def write_resource_indexes(manifest):
    # Bound generated C++ units for the 32-bit Windows compiler's address space.
    def write(path,names):
        entries='\n'.join('    <file>seasonal/'+name+'</file>' for name in names)
        path.write_text('<RCC><qresource prefix="/">\n'+entries+'\n</qresource></RCC>\n',encoding='utf-8')
    write(ROOT/'res/Seasonal.qrc',['manifest.json'])
    expected={ROOT/f'res/Seasonal_{theme}_{variant}.qrc' for theme,variants in manifest.items() for variant in variants}
    for stale in (ROOT/'res').glob('Seasonal_*.qrc'):
        if stale not in expected: stale.unlink()
    keep={meta['file'] for variants in manifest.values() for meta in variants.values()}
    for stale in DEST.glob('*.png'):
        if stale.name not in keep:stale.unlink()
    for theme,variants in manifest.items():
        for variant,meta in variants.items():
            write(ROOT/f'res/Seasonal_{theme}_{variant}.qrc',[meta['file']])
if __name__=='__main__':main()
