#!/bin/bash
# download-artifact flattens a single named artifact but nests a pattern match.
# '|| true': callers set -e, and the assignment would take find's exit status.
TARBALL=$(find download-artifact -name artifacts.tgz -print -quit 2>/dev/null || true)

if [ -z "$TARBALL" ]; then
    echo "ERROR: no artifacts.tgz under download-artifact/" >&2
    find download-artifact -maxdepth 3 2>/dev/null >&2 || echo "(directory does not exist)" >&2
    exit 1
fi

# No -C: MSYS tar cannot chdir into the backslashed $GITHUB_WORKSPACE on Windows.
tar xzf "$TARBALL"

# The separately licensed original transport is built for this GUI target.
if [ -d csqtt-artifact ]; then
    CSQTT_DEST="deployment/${DEST_SUFFIX%-system-qt}"
    mkdir -p "$CSQTT_DEST"
    cp csqtt-artifact/* "$CSQTT_DEST/"
fi
