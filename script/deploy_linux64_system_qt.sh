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
rm -rf deployment/${DEST_SUFFIX%-system-qt}

# handle debug info
objcopy --only-keep-debug $DEST/qThrone $DEST/qThrone.debug
strip --strip-debug --strip-unneeded $DEST/qThrone
objcopy --add-gnu-debuglink=$DEST/qThrone.debug $DEST/qThrone
