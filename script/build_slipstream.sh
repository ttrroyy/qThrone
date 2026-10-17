#!/usr/bin/env bash
# slipstream-client (Mygod/slipstream-rust) for the SlipStream DNS tunnel. The
# qThrone helper (script/dnstun) runs it behind its authenticated listener.
# Windows follows the project's own release build (vcpkg OpenSSL, picoquic
# through scripts/build_picoquic_windows.ps1); Linux and macOS link a vendored
# OpenSSL statically, so the binary needs no system library.
set -euo pipefail

revision=7de506b222c83ae97a19a618e33fe26899af1bd5
case "$RUNNER_OS:$ARCH" in
    Windows:x64) target=x86_64-pc-windows-msvc platform=x64 triplet=x64-windows-static-md host=x64-windows ;;
    Windows:arm64) target=aarch64-pc-windows-msvc platform=ARM64 triplet=arm64-windows-static-md host=arm64-windows ;;
    Windows:x86) echo "picoquic has no 32-bit Windows build: SlipStream is not part of this package"; exit 0 ;;
    macOS:x86_64) target=x86_64-apple-darwin ;;
    macOS:arm64) target=aarch64-apple-darwin ;;
    Linux:x86_64) target=x86_64-unknown-linux-gnu ;;
    Linux:aarch64) target=aarch64-unknown-linux-gnu ;;
    *) echo 'Unsupported Slipstream build target' >&2; exit 1 ;;
esac

python3 script/fetch_pinned_source.py https://github.com/Mygod/slipstream-rust.git "$revision" slipstream-source
git -C slipstream-source submodule update --init --recursive --depth 1
rustup toolchain install 1.97.1 --profile minimal
rustup target add --toolchain 1.97.1 "$target"
features=()
if [ "$RUNNER_OS" = Windows ]; then
    msvc_bin="$(cygpath -u "$VCToolsInstallDir")/bin/Host${VSCMD_ARG_HOST_ARCH}/${VSCMD_ARG_TGT_ARCH}"
    test -f "$msvc_bin/link.exe"
    export PATH="$msvc_bin:$PATH"
    vcpkg_root="${VCPKG_INSTALLATION_ROOT:-C:/vcpkg}"
    "$vcpkg_root/vcpkg.exe" install "pkgconf:$host" "openssl:$triplet"
    openssl_root="$(cygpath -w "$vcpkg_root/installed/$triplet")"
    for pkgconf in "$vcpkg_root/installed/$host/tools/pkgconf/pkgconf.exe" "$vcpkg_root/installed/$host/tools/pkgconf/pkg-config.exe"; do
        if [ -f "$pkgconf" ]; then export PKG_CONFIG="$(cygpath -w "$pkgconf")" PKG_CONFIG_EXECUTABLE="$(cygpath -w "$pkgconf")"; break; fi
    done
    export OPENSSL_ROOT_DIR="$openssl_root" OPENSSL_INCLUDE_DIR="$openssl_root\\include" OPENSSL_LIB_DIR="$openssl_root\\lib" \
        PKG_CONFIG_PATH="$openssl_root\\lib\\pkgconfig" OPENSSL_STATIC=1
    # The project's script names Visual Studio 2022; use the one this runner has.
    vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    vs_major=$("$vswhere" -latest -property installationVersion | cut -d. -f1)
    vs_year=$("$vswhere" -latest -property catalog_productLineVersion | tr -d '\r')
    sed -i "s/\"Visual Studio 17 2022\"/\"Visual Studio $vs_major $vs_year\"/" slipstream-source/scripts/build_picoquic_windows.ps1
    grep -q "Visual Studio $vs_major $vs_year" slipstream-source/scripts/build_picoquic_windows.ps1
    # The script hands its picoquic paths to the next build step through GITHUB_ENV.
    picoquic_env="$(mktemp)"
    (cd slipstream-source && GITHUB_ENV="$(cygpath -w "$picoquic_env")" pwsh -NoProfile -File scripts/build_picoquic_windows.ps1 -Platform "$platform")
    while IFS= read -r line; do
        line="${line%$'\r'}"; line="${line#$'\xef\xbb\xbf'}"
        [ -n "$line" ] && export "$line"
    done < "$picoquic_env"
else
    features=(--features openssl-vendored,picoquic-minimal-build)
    if [ "$RUNNER_OS" = macOS ]; then
        # picoquic's minimal build needs bash 4 (${var,,}); macOS ships 3.2. The project's own
        # macOS release builds the full library too.
        features=(--features openssl-vendored)
        export MACOSX_DEPLOYMENT_TARGET=10.15 CMAKE_OSX_ARCHITECTURES="${target%%-*}"
        [ "$CMAKE_OSX_ARCHITECTURES" = aarch64 ] && export CMAKE_OSX_ARCHITECTURES=arm64
    fi
fi
cargo +1.97.1 build --release --locked --target "$target" --manifest-path slipstream-source/Cargo.toml -p slipstream-client "${features[@]}"
mkdir -p csqtt-artifact
extension=''
if [ "$RUNNER_OS" = Windows ]; then extension=.exe; fi
cp "slipstream-source/target/$target/release/slipstream-client$extension" "csqtt-artifact/slipstream-client$extension"
cp slipstream-source/LICENSE csqtt-artifact/Slipstream-LICENSE
printf '%s\n' 'Original source: https://github.com/Mygod/slipstream-rust' "Revision: $revision" 'Desktop integration: script/dnstun in qThrone' > csqtt-artifact/Slipstream-SOURCE
tar -czf csqtt-artifact/slipstream-source.tar.gz --exclude=target --exclude=.git --exclude=.picoquic-build -C slipstream-source .
