"""Bake approved transparent artwork. Build machines need only the committed PNGs."""
from pathlib import Path
import importlib.util
import json
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / 'res/seasonal'
THEMES = {
    'halloween': (180, 100, 'frame'), 'newyear': (240, 100, 'frame'),
    'remembrance': (300, 100, 'draw'), 'may_remembrance': (120, 200, 'frame'),
    'kupala': (240, 100, 'frame'), 'patrick': (240, 100, 'frame'),
    'valentine': (240, 100, 'frame'), 'maslenitsa': (240, 100, 'frame'),
    'christmas': (240, 100, 'frame'), 'easter': (240, 100, 'frame'),
}

def main():
    DEST.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for theme, (count, interval, method) in THEMES.items():
        source = 'christmas_easter' if theme in ('christmas', 'easter') else theme
        spec = importlib.util.spec_from_file_location(source, ROOT / 'script/seasonal_sources' / (source + '.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        manifest[theme] = {}
        for variant in ('wide', 'compact', 'narrow'):
            render = getattr(module, method)
            def frame(t):
                return render(theme, variant, t) if source == 'christmas_easter' else render(variant, t)
            first = frame(0).convert('RGBA')
            w, h = first.size
            columns = 8 if h > 46 else 16
            atlas = Image.new('RGBA', (w * columns, h * ((count + columns - 1) // columns)))
            for index in range(count):
                art = frame(index * interval / 1000).convert('RGBA')
                assert art.size == first.size
                atlas.paste(art, ((index % columns) * w, (index // columns) * h))
            filename = theme + '-' + variant + '.png'
            atlas.save(DEST / filename, compress_level=9)
            manifest[theme][variant] = {'file': filename, 'width': w, 'height': h, 'columns': columns, 'frames': count, 'interval': interval}
            print(filename, (DEST / filename).stat().st_size, flush=True)
    (DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    entries = '\n'.join('    <file>seasonal/' + f.name + '</file>' for f in sorted(DEST.iterdir()) if f.suffix in ('.png', '.json'))
    (ROOT / 'res/Seasonal.qrc').write_text('<RCC><qresource prefix="/">\n' + entries + '\n</qresource></RCC>\n')

if __name__ == '__main__':
    main()
