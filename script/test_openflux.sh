#!/usr/bin/env bash
set -euo pipefail
export GOOS=linux GOARCH=amd64
DEST="$PWD/openflux-test-bin"
mkdir -p "$DEST"
GOCMD=go
source script/build_openflux.sh
# The local exit is OpenFlux's own command line; the client is the bundled helper.
(cd openflux-source && CGO_ENABLED=0 go build -trimpath -o "$DEST/openflux-exit" .)
# TestReorderBufferOrderAndSkip rewrites a package tunable that goroutines left by earlier
# tests still read: a race in the upstream test only, so it runs on its own without -race.
(cd openflux-source && go test -race -skip '^TestReorderBufferOrderAndSkip$' ./share ./socks5 ./transport ./tunnel)
(cd openflux-source && go test -run '^TestReorderBufferOrderAndSkip$' ./transport)
(cd openflux-source/mobile && go test -race -run 'ShareLink|SessionContext|ClassicShare|BuildSession' .)
python3 script/test_openflux.py "$DEST/openflux" "$DEST/openflux-exit"
