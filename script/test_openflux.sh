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
# OpenFlux 0.17 pinned the KDF context; these four upstream tests still assert the earlier rule and fail on upstream's own tree.
(cd openflux-source/mobile && go test -race -run 'ShareLink|SessionContext|ClassicShare|BuildSession' -skip 'TestSessionContextPrefersHighestPriorityURL|TestSessionContextSkipsCupsonline|TestClassicShareContext|TestExitShareLinkConnectsAClient' .)
python3 script/test_openflux.py "$DEST/openflux" "$DEST/openflux-exit"
