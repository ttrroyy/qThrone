#!/usr/bin/env bash
# The original MRTT client; only how it gets its packets on desktop systems differs from the Android build.
set -euo pipefail

revision=233eddc06a0a117d3b1cbd20386be2bb4300126e
python3 script/fetch_pinned_source.py https://github.com/ttrroyy/MRTT.git "$revision" mrtt-source
python3 script/prepare_mrtt.py mrtt-source
rustup toolchain install 1.97.1 --profile minimal

case "$RUNNER_OS:$ARCH" in
    Windows:x64) target=x86_64-pc-windows-msvc ;;
    Windows:arm64) target=aarch64-pc-windows-msvc ;;
    Windows:x86) target=i686-pc-windows-msvc ;;
    macOS:x86_64) target=x86_64-apple-darwin ;;
    macOS:arm64) target=aarch64-apple-darwin ;;
    Linux:x86_64) target=x86_64-unknown-linux-gnu ;;
    Linux:aarch64) target=aarch64-unknown-linux-gnu ;;
    *) echo 'Unsupported MRTT build target' >&2; exit 1 ;;
esac
rustup target add --toolchain 1.97.1 "$target"
if [ "$RUNNER_OS" = Windows ]; then
    # Git Bash prepends its own link.exe; Rust must use the MSVC linker.
    msvc_bin="$(cygpath -u "$VCToolsInstallDir")/bin/Host${VSCMD_ARG_HOST_ARCH}/${VSCMD_ARG_TGT_ARCH}"
    test -f "$msvc_bin/link.exe"
    export PATH="$msvc_bin:$PATH"
fi
if [ "$RUNNER_OS" = macOS ]; then export MACOSX_DEPLOYMENT_TARGET=10.15; fi
cargo +1.97.1 build --release --locked --target "$target" --manifest-path mrtt-source/rust-client/Cargo.toml
mkdir -p csqtt-artifact
extension=''
if [ "$RUNNER_OS" = Windows ]; then extension=.exe; fi
cp "mrtt-source/rust-client/target/$target/release/client$extension" "csqtt-artifact/mrtt-transport$extension"
cp mrtt-source/LICENSE csqtt-artifact/MRTT-LICENSE
printf '%s\n' 'Original source: https://github.com/ttrroyy/MRTT' "Revision: $revision" 'Desktop changes: script/prepare_mrtt.py in Rawline' > csqtt-artifact/MRTT-SOURCE
tar -czf csqtt-artifact/mrtt-source.tar.gz --exclude=target --exclude=.git -C mrtt-source rust-client shared LICENSE
