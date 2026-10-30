#!/usr/bin/env bash
set -euo pipefail

revision=2066156119a5afe7d63e104aefdc8e0849999d03
python3 script/fetch_pinned_source.py https://github.com/ttrroyy/csqttr.git "$revision" csqtt-source
python3 script/prepare_csqtt.py csqtt-source
# CSQTT 2.1.9, the original device-bound protocol, for profiles that pick it.
compat_revision=446293aa2e873ac5323ef6fd2316d9b81d966c11
python3 script/fetch_pinned_source.py https://github.com/ttrroyy/csqttr.git "$compat_revision" csqtt-compat-source
python3 script/prepare_csqtt_compat.py csqtt-compat-source
rustup toolchain install 1.97.1 --profile minimal

case "$RUNNER_OS:$ARCH" in
    Windows:x64) target=x86_64-pc-windows-msvc ;;
    Windows:arm64) target=aarch64-pc-windows-msvc ;;
    Windows:x86) target=i686-pc-windows-msvc ;;
    macOS:x86_64) target=x86_64-apple-darwin ;;
    macOS:arm64) target=aarch64-apple-darwin ;;
    Linux:x86_64) target=x86_64-unknown-linux-gnu ;;
    Linux:aarch64) target=aarch64-unknown-linux-gnu ;;
    *) echo 'Unsupported CSQTT build target' >&2; exit 1 ;;
esac
rustup target add --toolchain 1.97.1 "$target"
if [ "$RUNNER_OS" = Windows ]; then
    # Git Bash prepends its own link.exe; Rust must use the MSVC linker.
    msvc_bin="$(cygpath -u "$VCToolsInstallDir")/bin/Host${VSCMD_ARG_HOST_ARCH}/${VSCMD_ARG_TGT_ARCH}"
    test -f "$msvc_bin/link.exe"
    export PATH="$msvc_bin:$PATH"
fi
if [ "$RUNNER_OS" = macOS ]; then export MACOSX_DEPLOYMENT_TARGET=10.15; fi
cargo +1.97.1 build --release --locked --target "$target" --manifest-path csqtt-source/rust-client/Cargo.toml
cargo +1.97.1 build --release --locked --target "$target" --manifest-path csqtt-compat-source/rust-client/Cargo.toml
mkdir -p csqtt-artifact
extension=''
if [ "$RUNNER_OS" = Windows ]; then extension=.exe; fi
cp "csqtt-source/rust-client/target/$target/release/client$extension" "csqtt-artifact/csqtt-transport$extension"
cp csqtt-source/LICENSE csqtt-artifact/CSQTT-LICENSE
printf '%s\n' 'Original source: https://github.com/ttrroyy/csqttr (legacy-2.5.0)' "Revision: $revision" 'Desktop changes: script/prepare_csqtt.py in qThrone' > csqtt-artifact/CSQTT-SOURCE
tar -czf csqtt-artifact/csqtt-source.tar.gz --exclude=target --exclude=.git -C csqtt-source rust-client shared LICENSE
cp "csqtt-compat-source/rust-client/target/$target/release/client$extension" "csqtt-artifact/csqtt-compat$extension"
cp csqtt-compat-source/LICENSE csqtt-artifact/CSQTT-Compat-LICENSE
printf '%s
' 'Original source: https://github.com/ttrroyy/csqttr (legacy-2.1.9)' "Revision: $compat_revision" 'Desktop changes: script/prepare_csqtt_compat.py in qThrone' > csqtt-artifact/CSQTT-Compat-SOURCE
tar -czf csqtt-artifact/csqtt-compat-source.tar.gz --exclude=target --exclude=.git -C csqtt-compat-source rust-client shared LICENSE
