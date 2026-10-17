#!/usr/bin/env bash
# DNSTT, VayDNS and the Slipstream front run through VayDNS's client library,
# MasterDNS through MasterDnsVPN's own client, S3 Bypass through the fedarisha
# outbound of Xray-core-fedarisha. Each is the original project's code behind
# an authenticated loopback SOCKS5 listener (script/dnstun, masterdns, s3bypass).
set -euo pipefail
suffix=$([[ "$GOOS" == windows ]] && echo '.exe' || true)

vaydns_revision=a0ff70110d5e96686ab8f4c8e1c44cd09be07a75
if [ ! -d vaydns-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/net2share/vaydns.git "$vaydns_revision" vaydns-source
fi
test "$(git -C vaydns-source rev-parse HEAD)" = "$vaydns_revision"
mkdir -p vaydns-source/cmd/qthrone-dnstun
cp script/dnstun/*.go vaydns-source/cmd/qthrone-dnstun/
pushd vaydns-source
# The SSH and Shadowsocks backends of the dnst:// format.
"$GOCMD" get github.com/shadowsocks/go-shadowsocks2@v0.1.5
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w' -o "$DEST/dnstun$suffix" ./cmd/qthrone-dnstun
cp COPYING "$DEST/VayDNS-LICENSE"
printf '%s\n' 'https://github.com/net2share/vaydns' "Revision: $vaydns_revision" 'Desktop integration: script/dnstun in qThrone' > "$DEST/VayDNS-SOURCE"
tar -czf "$DEST/vaydns-source.tar.gz" --exclude=.git --exclude=.github .
popd

masterdns_revision=acbf1c61f90786f41b975d2e2f616afbce292b29
if [ ! -d masterdns-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/masterking32/MasterDnsVPN.git "$masterdns_revision" masterdns-source
fi
test "$(git -C masterdns-source rev-parse HEAD)" = "$masterdns_revision"
mkdir -p masterdns-source/cmd/qthrone-masterdns
cp script/masterdns/*.go masterdns-source/cmd/qthrone-masterdns/
pushd masterdns-source
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w' -o "$DEST/masterdns$suffix" ./cmd/qthrone-masterdns
cp LICENSE "$DEST/MasterDnsVPN-LICENSE"
printf '%s\n' 'https://github.com/masterking32/MasterDnsVPN' "Revision: $masterdns_revision" 'Desktop integration: script/masterdns in qThrone' > "$DEST/MasterDnsVPN-SOURCE"
tar -czf "$DEST/masterdns-source.tar.gz" --exclude=.git --exclude=.github .
popd

# Xray-core-fedarisha needs Go 1.27, newer than the patched toolchain of the
# Windows 7 and macOS 10.15 builds: S3 Bypass is not part of those.
if [[ "${IS_LEGACY:-false}" == true ]]; then
    echo "S3 Bypass is not built for legacy systems"
else
    fedarisha_revision=a04f4c09972b2aa7d6255197732c1c20e3fdc8a1
    if [ ! -d fedarisha-source/.git ]; then
        python3 script/fetch_pinned_source.py https://github.com/Fedarisha/Xray-core-fedarisha.git "$fedarisha_revision" fedarisha-source
    fi
    test "$(git -C fedarisha-source rev-parse HEAD)" = "$fedarisha_revision"
    mkdir -p fedarisha-source/main/qthrone-s3bypass
    cp script/s3bypass/*.go fedarisha-source/main/qthrone-s3bypass/
    pushd fedarisha-source
    CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w -buildid= -checklinkname=0' -o "$DEST/s3bypass$suffix" ./main/qthrone-s3bypass
    cp LICENSE "$DEST/Xray-fedarisha-LICENSE"
    printf '%s\n' 'https://github.com/Fedarisha/Xray-core-fedarisha' "Revision: $fedarisha_revision" 'Desktop integration: script/s3bypass in qThrone' > "$DEST/Xray-fedarisha-SOURCE"
    tar -czf "$DEST/fedarisha-source.tar.gz" --exclude=.git --exclude=.github .
    popd
fi
