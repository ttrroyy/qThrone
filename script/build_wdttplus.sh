#!/usr/bin/env bash
# WDTT Plus's own native client (go_client of Ivan4537/WDTT-Plus), built as its app builds
# libclient.so. qThrone's qwdtt helper starts it (-wdttplus-config) and answers its captcha.
set -euo pipefail
suffix=$([[ "$GOOS" == windows ]] && echo '.exe' || true)

wdttplus_revision=f706ccd373685bbde20cae33b2b7f400e8335ecc
if [ ! -d wdttplus-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/Ivan4537/WDTT-Plus.git "$wdttplus_revision" wdttplus-source
fi
test "$(git -C wdttplus-source rev-parse HEAD)" = "$wdttplus_revision"
pushd wdttplus-source/go_client
CGO_ENABLED=0 "$GOCMD" build -buildvcs=false -trimpath -ldflags='-s -w -checklinkname=0' -o "$DEST/wdttplus-client$suffix" .
popd
cp wdttplus-source/LICENSE "$DEST/WDTT-Plus-LICENSE"
printf '%s\n' 'https://github.com/Ivan4537/WDTT-Plus' "Revision: $wdttplus_revision" 'Desktop integration: qwdtt/wdttplus_bridge.go in qThrone' > "$DEST/WDTT-Plus-SOURCE"
# The binary's corresponding source: the client, its local module and the license.
tar -czf "$DEST/wdttplus-source.tar.gz" -C wdttplus-source go_client pathprobe LICENSE
