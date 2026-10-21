"""Fetch a pinned source, using our verified snapshot if the upstream disappears."""
import argparse
import hashlib
import json
import subprocess
import tempfile
import urllib.request
from pathlib import Path

def fetch(repository, revision, destination, snapshot_only=False):
    root = Path(__file__).resolve().parent.parent
    lock = json.loads((root / 'transport-sources.lock.json').read_text())
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'init', '-q', str(destination)], check=True)
    fetched = False
    if not snapshot_only:
        try:
            result = subprocess.run(['git', '-C', str(destination), 'fetch', '--depth=1', repository, revision], timeout=180)
            fetched = result.returncode == 0
        except subprocess.TimeoutExpired:
            fetched = False
    if not fetched:
        entry = lock[repository]
        assert entry['revision'] == revision, 'No verified source snapshot for this revision'
        if 'url' not in entry or 'sha256' not in entry:
            raise RuntimeError('Pinned upstream source is unavailable; no verified fallback snapshot')
        with urllib.request.urlopen(entry['url'], timeout=60) as response:
            payload = response.read()
        assert hashlib.sha256(payload).hexdigest() == entry['sha256'], 'Source snapshot checksum mismatch'
        with tempfile.TemporaryDirectory() as folder:
            bundle = Path(folder) / 'source.bundle'
            bundle.write_bytes(payload)
            subprocess.run(['git', '-C', str(destination), 'bundle', 'verify', str(bundle)], check=True)
            subprocess.run(['git', '-C', str(destination), 'fetch', str(bundle), 'refs/heads/qthrone-source-pin'], check=True)
    actual = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'FETCH_HEAD'], text=True).strip()
    assert actual == revision, 'Unexpected source revision'
    subprocess.run(['git', '-C', str(destination), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('repository')
    parser.add_argument('revision')
    parser.add_argument('destination')
    parser.add_argument('--snapshot-only', action='store_true')
    args = parser.parse_args()
    fetch(args.repository, args.revision, args.destination, args.snapshot_only)
