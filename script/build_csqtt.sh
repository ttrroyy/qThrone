#!/usr/bin/env bash
set -euo pipefail

revision=71712b07dbabe16fa43f5a248dc69e1ab9f3a270
git init csqtt-source
git -C csqtt-source fetch --depth=1 https://github.com/amurcanov/csqtt.git "$revision"
git -C csqtt-source checkout FETCH_HEAD
python3 script/prepare_csqtt.py csqtt-source
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
if [ "$RUNNER_OS" = macOS ]; then export MACOSX_DEPLOYMENT_TARGET=10.15; fi
cargo +1.97.1 build --release --locked --target "$target" --manifest-path csqtt-source/rust-client/Cargo.toml
mkdir -p csqtt-artifact
extension=''
if [ "$RUNNER_OS" = Windows ]; then extension=.exe; fi
cp "csqtt-source/rust-client/target/$target/release/client$extension" "csqtt-artifact/csqtt-transport$extension"
cp csqtt-source/LICENSE csqtt-artifact/CSQTT-LICENSE
printf '%s\n' 'Original source: https://github.com/amurcanov/csqtt' "Revision: $revision" 'Desktop changes: script/prepare_csqtt.py in qThrone' > csqtt-artifact/CSQTT-SOURCE
tar -czf csqtt-artifact/csqtt-source.tar.gz --exclude=target --exclude=.git -C csqtt-source rust-client shared LICENSE
