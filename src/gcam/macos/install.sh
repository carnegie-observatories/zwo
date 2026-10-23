#!/bin/bash
#
# Run by the macOS self-extracting installer (gcamzwo-macos-*.run) from the
# directory it unpacked into; see .github/workflows/ci_gcam.yml.
#
#   $APPDIR/gcamzwo.app          default /Applications
#   $PREFIX/bin/gcamzwo          default /usr/local, needs sudo on
#   $PREFIX/bin/getimages        Apple Silicon
#
# Guiders made from an earlier gcamzwo.app (gcam-<name>.app) are copies and
# are not touched; copy the new bundle over them.

set -e

PREFIX="${PREFIX:-/usr/local}"
APPDIR="${APPDIR:-/Applications}"

for dir in "$PREFIX/bin" "$APPDIR"; do
  if ! mkdir -p "$dir" 2>/dev/null || [ ! -w "$dir" ]; then
    echo "Cannot write to $dir: re-run with sudo, or set PREFIX=<dir> APPDIR=<dir>" >&2
    exit 1
  fi
done

install -m 755 gcamzwo.app/Contents/MacOS/gcamzwo getimages "$PREFIX/bin/"
echo "Installed gcamzwo and getimages into $PREFIX/bin"

rm -rf "$APPDIR/gcamzwo.app"
cp -R gcamzwo.app "$APPDIR/"
echo "Installed gcamzwo.app into $APPDIR"
