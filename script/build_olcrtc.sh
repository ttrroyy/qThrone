#!/usr/bin/env bash
set -euo pipefail
olcrtc_revision=f3ad8fb7c0d0fa981423f83269526fa94077c23d
if [ ! -d olcrtc-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/openlibrecommunity/olcrtc.git "$olcrtc_revision" olcrtc-source
fi
test "$(git -C olcrtc-source rev-parse HEAD)" = "$olcrtc_revision"
mkdir -p olcrtc-source/cmd/rawline-olcrtc
cp script/olcrtc/*.go olcrtc-source/cmd/rawline-olcrtc/
pushd olcrtc-source
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w' -o "$DEST/olcrtc$([[ "$GOOS" == windows ]] && echo '.exe')" ./cmd/rawline-olcrtc
cp LICENSE "$DEST/OLCRTC-LICENSE"
printf '%s\n' 'https://github.com/openlibrecommunity/olcrtc' "Revision: $olcrtc_revision" 'Desktop integration: script/olcrtc in Rawline' > "$DEST/OLCRTC-SOURCE"
tar -czf "$DEST/olcrtc-source.tar.gz" --exclude=.git --exclude=.github .
popd
