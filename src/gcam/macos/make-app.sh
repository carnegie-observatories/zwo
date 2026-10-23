#!/bin/sh
#
# Assemble gcamzwo.app, the macOS application bundle, around a gcamzwo that
# has already been built in src/gcam.  CI runs this (ci_gcam.yml) and puts
# the bundle into the installer; it works the same in a laptop checkout.
#
#   usage: make-app.sh [output-dir]        (default: src/gcam)
#
#   gcamzwo.app/Contents/
#     Info.plist              version from P_VERSION in zwogcam.h
#     MacOS/gcam-launch       CFBundleExecutable, see gcam-launch.c
#     MacOS/gcamzwo           the real binary
#     Resources/gcamzwo.icns  from docs/ZWO/gcamzwo.png
#
# The bundle is generic.  A site makes a guider out of it by copying it
# under the name of an .ini file: gcam-pfs-sv.app loads pfs-sv.ini.  The
# name of a bundle is not part of its signature, so a copy stays valid.
#
# CODESIGN_IDENTITY selects the signing identity; the default "-" signs
# ad hoc, like the linker does.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
gcam=$(dirname "$here")
out=${1:-$gcam}
app="$out/gcamzwo.app"
png="$gcam/../../docs/ZWO/gcamzwo.png"

[ -x "$gcam/gcamzwo" ] || {
    echo "make-app: $gcam/gcamzwo not found, build it first" >&2
    exit 1
}

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$gcam/gcamzwo" "$app/Contents/MacOS/"

# The launcher, for the architectures gcamzwo was built for.
archs=""
for a in $(lipo -archs "$gcam/gcamzwo"); do archs="$archs -arch $a"; done
# shellcheck disable=SC2086
${CC:-/usr/bin/cc} $archs -O -Wall -o "$app/Contents/MacOS/gcam-launch" \
    "$here/gcam-launch.c"

# Version: CI has stamped P_VERSION as "<version>-<short-sha>" by now
# (etc/scripts/stamp-version.sh); a local build still has the bare version
# and takes the commit from git.  As in the ZWOFinder bundle, the version
# goes into CFBundleShortVersionString and the commit into CFBundleVersion.
pver=$(sed -n 's/^#define[[:space:]][[:space:]]*P_VERSION[[:space:]][[:space:]]*"\([^"]*\)".*/\1/p' "$gcam/zwogcam.h")
[ -n "$pver" ] || {
    echo "make-app: no '#define P_VERSION \"...\"' in $gcam/zwogcam.h" >&2
    exit 1
}
version=$pver
build=$(git -C "$gcam" rev-parse --short HEAD 2>/dev/null || true)
case "$pver" in
*-*)
    sha=${pver##*-}
    if printf '%s' "$sha" | grep -Eq '^[0-9a-f]{7,40}$'; then
        version=${pver%-*}
        build=$sha
    fi
    ;;
esac
[ -n "$build" ] || build=$version

plist="$app/Contents/Info.plist"
cp "$here/Info.plist" "$plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $build"              "$plist"

# Icon: the control panel, i.e. the left square of the screenshot.  A crop
# offset of 0 means "centered" to sips, hence the 1.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
side=$(sips -g pixelHeight "$png" | awk '/pixelHeight/ {print $2}')
sips -c "$side" "$side" --cropOffset 1 1 "$png" --out "$tmp/square.png" >/dev/null
mkdir "$tmp/gcamzwo.iconset"
for s in 16 32 128 256 512; do
    sips -z $s $s "$tmp/square.png" \
        --out "$tmp/gcamzwo.iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((2*s)) $((2*s)) "$tmp/square.png" \
        --out "$tmp/gcamzwo.iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns -o "$app/Contents/Resources/gcamzwo.icns" "$tmp/gcamzwo.iconset"

# Inside out: the nested binary first, then the bundle, which seals it.
identity=${CODESIGN_IDENTITY:--}
codesign --force --sign "$identity" "$app/Contents/MacOS/gcamzwo"
codesign --force --sign "$identity" "$app"
codesign --verify --strict --deep "$app"

echo "make-app: $app ($version, build $build)"
