#!/usr/bin/env bash
set -euo pipefail
export GOOS=linux GOARCH=amd64
DEST="$PWD/openflux-test-bin"
mkdir -p "$DEST"
GOCMD=go
source script/build_openflux.sh
# The local exit is OpenFlux's own command line; the client is the bundled helper.
(cd openflux-source && CGO_ENABLED=0 go build -trimpath -o "$DEST/openflux-exit" .)
(cd openflux-source && go test -race ./share ./socks5 ./transport ./tunnel)
(cd openflux-source/mobile && go test -race -run 'ShareLink|SessionContext|ClassicShare|BuildSession' .)
python3 script/test_openflux.py "$DEST/openflux" "$DEST/openflux-exit"
