#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
source "$(dirname "$0")/extract_core_artifact.sh"

mv deployment/$DEST_SUFFIX/* $GITHUB_WORKSPACE/build/qThrone.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $GITHUB_WORKSPACE/build
macdeployqt qThrone.app -verbose=3
popd

codesign --force --deep --sign - $GITHUB_WORKSPACE/build/qThrone.app

dsymutil $GITHUB_WORKSPACE/build/qThrone.app/Contents/MacOS/qThrone
strip -S $GITHUB_WORKSPACE/build/qThrone.app/Contents/MacOS/qThrone

mv $GITHUB_WORKSPACE/build/qThrone.app $DEST
