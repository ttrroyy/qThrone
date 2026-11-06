#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
source "$(dirname "$0")/extract_core_artifact.sh"

mv deployment/$DEST_SUFFIX/* $GITHUB_WORKSPACE/build/Rawline.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $GITHUB_WORKSPACE/build
macdeployqt Rawline.app -verbose=3
popd

dsymutil $GITHUB_WORKSPACE/build/Rawline.app/Contents/MacOS/Rawline
strip -S $GITHUB_WORKSPACE/build/Rawline.app/Contents/MacOS/Rawline

# Stripping changes the executable, so sign the final bundle after that step.
codesign --force --deep --sign - $GITHUB_WORKSPACE/build/Rawline.app
codesign --verify --deep --strict $GITHUB_WORKSPACE/build/Rawline.app

mv $GITHUB_WORKSPACE/build/Rawline.app $DEST
