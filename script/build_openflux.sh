#!/usr/bin/env bash
set -euo pipefail
revision=edcf77a1848a72370cfba9f346d3b897b61e7e7d
if [ ! -d openflux-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/p1neappleXpress/OpenFlux.git "$revision" openflux-source
fi
test "$(git -C openflux-source rev-parse HEAD)" = "$revision"
# The client runs through OpenFlux's own mobile package, as the original apps do.
mkdir -p openflux-source/mobile/cmd/qthrone-openflux
cp script/openflux/*.go openflux-source/mobile/cmd/qthrone-openflux/
pushd openflux-source/mobile
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w -checklinkname=0' -o "$DEST/openflux$([[ "$GOOS" == windows ]] && echo '.exe')" ./cmd/qthrone-openflux
popd
pushd openflux-source
cp LICENSE "$DEST/OpenFlux-LICENSE"
printf '%s\n' 'https://github.com/p1neappleXpress/OpenFlux' "Revision: $revision" 'Desktop integration: script/openflux in qThrone' > "$DEST/OpenFlux-SOURCE"
tar -czf "$DEST/openflux-source.tar.gz" --exclude=.git --exclude=.idea --exclude=ios-app --exclude=docker --exclude=.github .
popd
