"""Package the Linux archives produced by the same release workflow."""
import hashlib
import re
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
tag, archives, destination = sys.argv[1:]
if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?(?:-[A-Za-z0-9.]+)?', tag):
    raise SystemExit('Invalid release version')
archives = Path(archives).resolve()
destination = Path(destination).resolve()
destination.mkdir(parents=True, exist_ok=True)
recipe = (root / 'aur/rawline-bin/PKGBUILD').read_text(encoding='utf-8')
recipe = re.sub(r'^pkgver=.*$', 'pkgver=' + tag.replace('-', '_'), recipe, flags=re.M)
for arch, upstream in (('x86_64', 'amd64'), ('aarch64', 'arm64')):
    archive = archives / f'Rawline-{tag}-linux-{upstream}.zip'
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    recipe = re.sub(r'^source_' + arch + r'=.*$',
                    f"source_{arch}=('{archive.name}::{archive.as_uri()}')", recipe, flags=re.M)
    recipe = re.sub(r'^sha256sums_' + arch + r'=.*$',
                    f"sha256sums_{arch}=('{digest}')", recipe, flags=re.M)
    print(archive.name, digest)
for name in ('rawline', 'Rawline.desktop'):
    shutil.copyfile(root / 'aur/rawline-bin' / name, destination / name)
(destination / 'PKGBUILD').write_text(recipe, encoding='utf-8')
