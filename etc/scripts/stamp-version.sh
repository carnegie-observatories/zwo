#!/bin/sh
#
# Stamp P_VERSION in a source header with the build's real version, so the
# binary reports what it actually is instead of whatever was last hand-edited
# into the header.  Run from CI *before* building; see .github/workflows/.
#
# This is the C analogue of what the shared macOS-app CI does to Info.plist
# (lco-software Swope/MIKE/MagE/...): stamp the source, build, and the
# resulting binary self-reports -- no build-system wiring, no Run Script
# phase, no change to how the program reads its own version.
#
#   usage: stamp-version.sh <header> <short-sha> [release-tag]
#
# The stamped value is "<base>-<short-sha>", where <base> is the release tag
# when building a release and otherwise the version already declared in the
# header.  Unlike the macOS apps, which stamp a placeholder 0.0.0 on branch
# builds, the declared version is kept as the base here: P_VERSION is written
# into archived data (zwoserver's SOFTWARE card, gcam's FITS header), so the
# provenance of a frame has to stay readable.  The short SHA is what
# identifies the build exactly.
#
# Fails loudly if the #define is not found -- a silent miss would ship
# binaries claiming a stale version, which is the whole problem this solves.

set -eu

header=${1:?usage: stamp-version.sh <header> <short-sha> [release-tag]}
short_sha=${2:?usage: stamp-version.sh <header> <short-sha> [release-tag]}
tag=${3:-}

[ -f "$header" ] || { echo "stamp-version: no such file: $header" >&2; exit 1; }

# The version currently declared in the header.
base=$(sed -n 's/^#define[[:space:]][[:space:]]*P_VERSION[[:space:]][[:space:]]*"\([^"]*\)".*/\1/p' "$header")
[ -n "$base" ] || {
    echo "stamp-version: no '#define P_VERSION \"...\"' in $header" >&2
    exit 1
}
# Release tags are "v1.0.7"; P_VERSION holds the bare number, and the code
# formats it as "-v%s" (zwogcam.c, zwoserver.c), so keeping the tag's "v"
# would render "ZwoGcam-vv1.0.7".  Strip it.
[ -z "$tag" ] || base=$(printf '%s' "$tag" | sed 's/^[vV]//')

version="${base}-${short_sha}"

# Replace only the quoted string, so any trailing comment on the line
# (src/server/zwo.h carries the SDK version there) survives.
sed 's|\(^#define[[:space:]][[:space:]]*P_VERSION[[:space:]][[:space:]]*\)"[^"]*"|\1"'"$version"'"|' \
    "$header" > "$header.stamped"
mv "$header.stamped" "$header"

got=$(sed -n 's/^#define[[:space:]][[:space:]]*P_VERSION[[:space:]][[:space:]]*"\([^"]*\)".*/\1/p' "$header")
[ "$got" = "$version" ] || {
    echo "stamp-version: $header still reads '$got', wanted '$version'" >&2
    exit 1
}

echo "stamp-version: $header P_VERSION=\"$version\""
