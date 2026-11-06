#!/usr/bin/env bash
# FreeTurn's client session behind the authenticated SOCKS bridge in freeturn/, built inside its pinned source for its internal packages.
set -euo pipefail
suffix=$([[ "$GOOS" == windows ]] && echo '.exe' || true)

freeturn_revision=6d857e1e847a38fdee3e150314fb37009e3c37a6
gvisor_version=v0.0.0-20260906120324-45bde0d1defa
if [ ! -d freeturn-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/hackdiaz-dev/free-turn-proxy.git "$freeturn_revision" freeturn-source
fi
test "$(git -C freeturn-source rev-parse HEAD)" = "$freeturn_revision"
mkdir -p freeturn-source/cmd/rawline
cp freeturn/*.go freeturn-source/cmd/rawline/
pushd freeturn-source
"$GOCMD" get "gvisor.dev/gvisor@$gvisor_version"
CGO_ENABLED=0 "$GOCMD" build -mod=mod -buildvcs=false -trimpath -ldflags='-s -w -checklinkname=0' -o "$DEST/freeturn-client$suffix" ./cmd/rawline
cp LICENSE "$DEST/FreeTurn-LICENSE"
printf '%s\n' 'https://github.com/hackdiaz-dev/free-turn-proxy' "Revision: $freeturn_revision" 'Desktop integration: freeturn/' > "$DEST/FreeTurn-SOURCE"
tar -czf "$DEST/freeturn-source.tar.gz" --exclude=.git .
popd
