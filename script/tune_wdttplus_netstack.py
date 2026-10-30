"""Give WDTT Plus's SOCKS5 netstack the TCP tuning of qThrone's own qWDTT helper (qwdtt/netstack_tuning.go)."""
from pathlib import Path
import shutil
import sys

qwdtt, client = Path(sys.argv[1]), Path(sys.argv[2])
path = client / "socks_wg.go"
source = path.read_text(encoding="utf-8")
old = 'return nil, nil, fmt.Errorf("создание userspace TUN: %w", err)\n\t}\n'
if source.count(old) != 1:
    raise SystemExit("Pinned WDTT Plus netstack changed; review the TCP tuning patch")
path.write_text(source.replace(old, old + "\ttuneNetstack(tunnelNetwork)\n", 1), encoding="utf-8", newline="\n")
shutil.copyfile(qwdtt / "netstack_tuning.go", client / "netstack_tuning.go")
