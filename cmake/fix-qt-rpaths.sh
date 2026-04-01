#!/bin/bash
# fix-qt-rpaths.sh — rewrites absolute Qt framework load commands to @rpath.
# Usage: fix-qt-rpaths.sh <binary>
set -e
BINARY="$1"

# Read every load command, find absolute Qt framework paths, rewrite them.
otool -L "$BINARY" | awk '/\/Qt[A-Za-z]+\.framework\//{print $1}' | while read -r old; do
    # Extract just the framework binary name: QtCore.framework/Versions/A/QtCore
    fw=$(echo "$old" | sed 's|.*\(Qt[A-Za-z]*\.framework/.*\)|\1|')
    new="@rpath/$fw"
    echo "  $old → $new"
    install_name_tool -change "$old" "$new" "$BINARY"
done
