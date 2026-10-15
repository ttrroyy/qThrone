"""Bake approved smooth artwork into transparent atlases; runtime needs no Pillow."""
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor
import sys,json
from PIL import Image
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'script/seasonal_hd'))
from render import load,render
DEST=ROOT/'res/seasonal'
THEMES={'halloween':18,'newyear':24,'remembrance':30,'may_remembrance':24,'kupala':24,'patrick':24,'valentine':24,'maslenitsa':24,'christmas':24,'easter':24,'spring':24,'autumn':24}
def bake(theme):
    module=load(theme);result={};count=THEMES[theme]*20
    for variant in ('wide','compact'):
        # Twice the logical resolution bounds active atlas memory to about 48 MiB.
        w,h=(272 if variant=='wide' else 184),92
        columns=8
        atlas=Image.new('RGBA',(w*columns,h*((count+columns-1)//columns)))
        for index in range(count):
            art=render(module,theme,variant,index/20).resize((w,h),Image.Resampling.LANCZOS)
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
    names=['manifest.json']+[v['file'] for t in manifest.values() for v in t.values()]
    entries='\n'.join('    <file>seasonal/'+name+'</file>' for name in sorted(names))
    (ROOT/'res/Seasonal.qrc').write_text('<RCC><qresource prefix="/">\n'+entries+'\n</qresource></RCC>\n',encoding='utf-8')
if __name__=='__main__':main()
