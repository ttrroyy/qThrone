#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy binary ####
cp $GITHUB_WORKSPACE/build/qThrone $DEST

#### copy qThrone.png ####
cp $GITHUB_WORKSPACE/res/public/Throne.png $DEST/qThrone.png

#### copy Core ####
source "$(dirname "$0")/extract_core_artifact.sh"
cp deployment/${DEST_SUFFIX%-system-qt}/qThroneCore $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/qThroneUpdater $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/qwdtt $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/qwdtt-LICENSE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/olcrtc $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/OLCRTC-LICENSE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/OLCRTC-SOURCE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/olcrtc-source.tar.gz $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/openflux $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/OpenFlux-LICENSE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/OpenFlux-SOURCE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/openflux-source.tar.gz $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/csqtt-transport $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/CSQTT-LICENSE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/CSQTT-SOURCE $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/csqtt-source.tar.gz $DEST
for name in csqtt-compat CSQTT-Compat-LICENSE CSQTT-Compat-SOURCE csqtt-compat-source.tar.gz; do cp deployment/${DEST_SUFFIX%-system-qt}/$name $DEST; done
for name in dnstun VayDNS-LICENSE VayDNS-SOURCE vaydns-source.tar.gz masterdns MasterDnsVPN-LICENSE MasterDnsVPN-SOURCE masterdns-source.tar.gz cottendns CottenDNS-LICENSE CottenDNS-SOURCE cottendns-source.tar.gz soar Soar-LICENSE Soar-SOURCE soar-source.tar.gz \
    openfluxkey OpenFluxKey-LICENSE OpenFluxKey-SOURCE openfluxkey-source.tar.gz \
    s3bypass Xray-fedarisha-LICENSE Xray-fedarisha-SOURCE fedarisha-source.tar.gz slipstream-client Slipstream-LICENSE Slipstream-SOURCE slipstream-source.tar.gz \
    wdttplus-client WDTT-Plus-LICENSE WDTT-Plus-SOURCE wdttplus-source.tar.gz \
    sstp-client SSTP-LICENSE SSTP-SOURCE sstp-source.tar.gz; do
    cp deployment/${DEST_SUFFIX%-system-qt}/$name $DEST
done
rm -rf deployment/${DEST_SUFFIX%-system-qt}

# handle debug info
objcopy --only-keep-debug $DEST/qThrone $DEST/qThrone.debug
strip --strip-debug --strip-unneeded $DEST/qThrone
objcopy --add-gnu-debuglink=$DEST/qThrone.debug $DEST/qThrone
