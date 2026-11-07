"""Desktop I/O of the original MRTT client: its transport code stays as it is."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
# Abstract Unix sockets exist only on Linux and Android. Desktops use the client's own UDP dispatcher.
linux = '#[cfg(any(target_os = "linux", target_os = "android"))]'
tun = root / "rust-client/tun.rs"
source = tun.read_text(encoding="utf-8")
assert "#[cfg(unix)]" in source and "#[cfg(not(unix))]" in source, "MRTT TUN handover changed"
source = source.replace("#[cfg(unix)]", linux)
source = source.replace("#[cfg(not(unix))]", '#[cfg(not(any(target_os = "linux", target_os = "android")))]')
source = source.replace("#[cfg(all(test, unix))]", '#[cfg(all(test, any(target_os = "linux", target_os = "android")))]')
tun.write_text(source, encoding="utf-8", newline="\n")
# Their fixtures use the same socket addresses; the UDP tests still run everywhere.
dispatcher = root / "rust-client/dispatcher.rs"
source = dispatcher.read_text(encoding="utf-8")
before, separator, tests = source.partition("#[cfg(test)]\nmod tests {")
assert separator, "MRTT dispatcher test boundary changed"
dispatcher.write_text(before + separator + tests.replace("#[cfg(unix)]", linux), encoding="utf-8", newline="\n")
