#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy exe ####
cp $GITHUB_WORKSPACE/build/qThrone.exe $DEST
cp $GITHUB_WORKSPACE/build/qThrone.pdb $DEST || true

source "$(dirname "$0")/extract_core_artifact.sh"
