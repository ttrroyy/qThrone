#!/usr/bin/env bash
# DNSTT, VayDNS and the Slipstream front run through VayDNS's client library,
# MasterDNS (and StormDNS) through MasterDnsVPN's own client, CottenDNS through
# its own client, Soar through Lantern's client library, S3 Bypass through the
# fedarisha outbound of Xray-core-fedarisha. Each is the original project's code
# behind an authenticated loopback SOCKS5 listener (script/dnstun, masterdns,
# cottendns, soar, s3bypass).
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

cottendns_revision=7db7b73658c791cce62300d29647e4d369d6e1e4
if [ ! -d cottendns-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/TaJirax/cottenDNS.git "$cottendns_revision" cottendns-source
fi
test "$(git -C cottendns-source rev-parse HEAD)" = "$cottendns_revision"
mkdir -p cottendns-source/cmd/qthrone-cottendns
cp script/cottendns/*.go cottendns-source/cmd/qthrone-cottendns/
pushd cottendns-source
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w' -o "$DEST/cottendns$suffix" ./cmd/qthrone-cottendns
cp LICENSE "$DEST/CottenDNS-LICENSE"
printf '%s\n' 'https://github.com/TaJirax/cottenDNS' "Revision: $cottendns_revision" 'Desktop integration: script/cottendns in qThrone' > "$DEST/CottenDNS-SOURCE"
tar -czf "$DEST/cottendns-source.tar.gz" --exclude=.git --exclude=.github .
popd

soar_revision=fea80aae6c60a9f27d7358e5f967cbc9b57bb37d
if [ ! -d soar-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/getlantern/soar.git "$soar_revision" soar-source
fi
test "$(git -C soar-source rev-parse HEAD)" = "$soar_revision"
mkdir -p soar-source/cmd/qthrone-soar
cp script/soar/*.go soar-source/cmd/qthrone-soar/
pushd soar-source
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w' -o "$DEST/soar$suffix" ./cmd/qthrone-soar
cp LICENSE "$DEST/Soar-LICENSE"
printf '%s\n' 'https://github.com/getlantern/soar' "Revision: $soar_revision" 'Desktop integration: script/soar in qThrone' > "$DEST/Soar-SOURCE"
tar -czf "$DEST/soar-source.tar.gz" --exclude=.git --exclude=.github .
popd

# Profiles of the wlruscfd/openflux-server fork (its openflux://import links): the fork's
# exit nodes frame traffic their own way, so they run through the fork's bridge, not OpenFlux's.
if [[ "${IS_LEGACY:-false}" == true ]]; then
    echo "OpenFlux Key is not built for legacy systems"
else
    openfluxkey_revision=fc5647767c9daba6571ec3157f9be9b6ffe6e551
    if [ ! -d openfluxkey-source/.git ]; then
        python3 script/fetch_pinned_source.py https://github.com/wlruscfd/openflux-server.git "$openfluxkey_revision" openfluxkey-source
    fi
    test "$(git -C openfluxkey-source rev-parse HEAD)" = "$openfluxkey_revision"
    mkdir -p openfluxkey-source/cmd/qthrone-openfluxkey
    cp script/openfluxkey/*.go openfluxkey-source/cmd/qthrone-openfluxkey/
    cp script/openfluxkey/bridge/*.go openfluxkey-source/bridge/
    pushd openfluxkey-source
    "$GOCMD" get github.com/chromedp/chromedp@v0.14.2
    CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w -checklinkname=0' -o "$DEST/openfluxkey$suffix" ./cmd/qthrone-openfluxkey
    cp LICENSE "$DEST/OpenFluxKey-LICENSE"
    printf '%s\n' 'https://github.com/wlruscfd/openflux-server' "Revision: $openfluxkey_revision" 'Desktop integration: script/openfluxkey in qThrone' > "$DEST/OpenFluxKey-SOURCE"
    tar -czf "$DEST/openfluxkey-source.tar.gz" --exclude=.git --exclude=.github .
    popd
fi

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
