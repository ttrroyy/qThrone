#!/bin/bash
set -euo pipefail

USAGE="usage: pack_release.sh <windows|linux-amd64|linux-arm64|macos|debug> <tag>"
TARGET="${1:?$USAGE}"
export TAG="${2:?$USAGE}"
export ROOT="$PWD"
export DEPLOY="$ROOT/deployment"
export WORK="$ROOT/.pack"
JOBS="${PACK_JOBS:-$(nproc)}"

rm -rf "$WORK"
mkdir -p "$WORK/logs" "$DEPLOY"

# Common tarballs are skipped: every build tarball already carries its core.
untar() {
    local glob="$1"
    shift
    find download-artifact -path "*/Rawline-*-$glob/artifacts.tgz" -not -path '*-Common-*' -print0 |
        xargs -0 -r -I{} -P "$JOBS" tar xzf {} "$@"
}

build_installer() {
    local version="${TAG#v}"
    version="${version#V}"
    local parts
    IFS='.' read -r -a parts <<<"${version%%-*}"
    local iscc="${ISCC:-$(command -v iscc.exe || echo '/c/Program Files (x86)/Inno Setup 6/ISCC.exe')}"
    # ISCC is a native Windows program: stop MSYS from rewriting its /-switches as paths.
    MSYS_NO_PATHCONV=1 "$iscc" /Q \
        "/DAppVersion=$version" \
        "/DAppVersionMajor=${parts[0]:-0}" \
        "/DAppVersionMinor=${parts[1]:-0}" \
        "/DAppVersionPatch=${parts[2]:-0}" \
        "/DAppVersionBuild=${parts[3]:-0}" \
        "/O$(cygpath -w "$DEPLOY")" \
        "/FRawline-$TAG-windows-universal-installer" \
        "$(cygpath -w "$ROOT/script/windows_installer.iss")"
}

# Hard links give the archive its Rawline/ root without moving a dir other tasks still read.
zip_dir() {
    mkdir -p "$WORK/$2"
    cp -al "$DEPLOY/$1" "$WORK/$2/Rawline"
    cd "$WORK/$2"
    if command -v zip >/dev/null; then
        zip -q -r "$DEPLOY/Rawline-$TAG-$2.zip" Rawline
    else
        # Windows runners ship 7-Zip but not Info-ZIP.
        7z a -tzip -mx=5 -bd -bso0 "$DEPLOY/Rawline-$TAG-$2.zip" Rawline
    fi
}

zip_app() {
    mkdir -p "$WORK/$2/Rawline"
    mv "$DEPLOY/$1/Rawline.app" "$WORK/$2/Rawline/"
    cd "$WORK/$2"
    zip -q --symlinks -r "$DEPLOY/Rawline-$TAG-$2.zip" Rawline
}

zip_debug() {
    cd "$DEPLOY"
    zip -q -r debug-symbols.zip debug
}

pack_deb() {
    cd "$DEPLOY"
    bash "$ROOT/script/pack_debian.sh" "$TAG" "$@"
}

pack_rpm() {
    cd "$DEPLOY"
    bash "$ROOT/script/pack_rpm.sh" "$TAG" "$@"
}

run_task() {
    local name="$1"
    shift
    # No `|| rc=$?` here: errexit is ignored for any command on the left of || or inside an if test.
    (set -eo pipefail; "$@") >"$WORK/logs/$name.log" 2>&1
    local rc=$?
    printf '%4ds  %s%s\n' "$SECONDS" "$name" "$([[ $rc == 0 ]] || echo ' FAILED')" >>"$WORK/logs/times"
    return $rc
}

export -f build_installer zip_dir zip_app zip_debug pack_deb pack_rpm run_task

case "$TARGET" in
windows)
    untar 'windows*' --exclude='*.pdb'
    TASKS=(
        "installer build_installer"
        "zip-windows64 zip_dir windows-amd64 windows64"
        "zip-windows-arm64 zip_dir windows-arm64 windows-arm64"
        "zip-windows32 zip_dir windowslegacy-386 windows32"
        "zip-windowslegacy64 zip_dir windowslegacy-amd64 windowslegacy64"
    )
    ;;
linux-amd64 | linux-arm64)
    arch="${TARGET#linux-}"
    untar "linux-$arch*" --exclude=Rawline.debug
    TASKS=(
        "deb-$arch pack_deb $arch"
        "rpm-$arch pack_rpm $arch"
        "deb-$arch-system-qt pack_deb $arch systemqt"
        "rpm-$arch-system-qt pack_rpm $arch systemqt"
        "zip-linux-$arch zip_dir linux-$arch linux-$arch"
    )
    ;;
macos)
    untar 'darwin*' --exclude=Rawline.dSYM
    TASKS=(
        "zip-macos-arm64 zip_app darwin-arm64 macos-arm64"
        "zip-macos-amd64 zip_app darwin-amd64 macos-amd64"
        "zip-macoslegacy-amd64 zip_app darwinlegacy-amd64 macoslegacy-amd64"
    )
    ;;
debug)
    untar linux-amd64 --wildcards '*/Rawline.debug'
    untar linux-arm64 --wildcards '*/Rawline.debug'
    untar 'windows*' --wildcards '*/Rawline.pdb'
    untar 'darwin*' --wildcards '*/Rawline.dSYM/*'
    cd "$DEPLOY"
    mkdir -p debug
    mv linux-amd64/Rawline.debug "debug/Rawline-$TAG-linux-amd64.debug"
    mv linux-arm64/Rawline.debug "debug/Rawline-$TAG-linux-arm64.debug"
    for dir in windows-amd64 windows-arm64 windowslegacy-386 windowslegacy-amd64; do
        mv "$dir/Rawline.pdb" "debug/Rawline-$TAG-$dir.pdb"
    done
    mv darwin-arm64/Rawline.app/Contents/MacOS/Rawline.dSYM "debug/Rawline-$TAG-macos-arm64.dSYM"
    mv darwin-amd64/Rawline.app/Contents/MacOS/Rawline.dSYM "debug/Rawline-$TAG-macos-amd64.dSYM"
    mv darwinlegacy-amd64/Rawline.app/Contents/MacOS/Rawline.dSYM "debug/Rawline-$TAG-macoslegacy-amd64.dSYM"
    TASKS=("debug-symbols zip_debug")
    ;;
*)
    echo "$USAGE" >&2
    exit 1
    ;;
esac

rc=0
printf '%s\n' "${TASKS[@]}" | xargs -L1 -P "$JOBS" bash -c 'run_task "$@"' _ || rc=$?

for log in "$WORK"/logs/*.log; do
    echo "::group::$(basename "$log" .log)"
    cat "$log"
    echo "::endgroup::"
done
echo "Pack task durations:"
sort -rn "$WORK/logs/times"
rm -rf "$WORK"
exit "$rc"
