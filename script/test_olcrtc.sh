#!/usr/bin/env bash
set -euo pipefail
export GOOS=linux GOARCH=amd64
DEST="$PWD/olcrtc-test-bin"
mkdir -p "$DEST"
GOCMD=go
source script/build_olcrtc.sh
(cd olcrtc-source && go test -race -timeout 180s ./cmd/rawline-olcrtc ./internal/crypto ./internal/handshake ./internal/client ./internal/server ./internal/transport/... ./pkg/olcrtc/client)
