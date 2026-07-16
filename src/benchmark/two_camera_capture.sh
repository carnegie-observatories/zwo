#!/usr/bin/env bash
#
# two_camera_capture.sh — capture two ZWO cameras simultaneously from one
# client (e.g. zwo-nuc), writing a per-frame CSV (timestamp + mean count)
# per camera. Point both cameras at the same blinking light; analyze with
# plot_two_camera_sync.py to recover the inter-camera timestamp delay.
#
# Both zwo_benchmark instances run on THIS host, so client_epoch_s in the
# two CSVs is a single common clock; server_ts_ns is each camera host's
# own clock (what we are validating).
#
# Usage:
#   ./two_camera_capture.sh [options]
#     -a HOST_A     camera A host/IP   (default 10.8.80.225)
#     -b HOST_B     camera B host/IP   (default 10.8.80.218)
#     -d SECONDS    capture duration   (default 60)
#     -e SECONDS    exposure           (default 0.005)
#     -n BIN        binning            (default 2)
#     -r PERCENT    ROI window %       (default 5)
#     -B BITS       pixel bits 8|16    (default 8)
#     -o DIR        output directory   (default ./sync_<UTC>)
#   Extra args after -- are passed to both zwo_benchmark instances,
#   e.g. -- --gain 200
#
# Run the light at ~1 Hz; a duration of 60 s gives ~60 flashes to fold.

set -euo pipefail

HOST_A=10.8.80.225
HOST_B=10.8.80.218
DUR=60
EXP=0.005
BIN=2
ROI=5
BITS=8
OUTDIR=""
BIN_EXE="$(cd "$(dirname "$0")" && pwd)/zwo_benchmark"

while getopts "a:b:d:e:n:r:B:o:h" opt; do
  case "$opt" in
    a) HOST_A="$OPTARG" ;;
    b) HOST_B="$OPTARG" ;;
    d) DUR="$OPTARG" ;;
    e) EXP="$OPTARG" ;;
    n) BIN="$OPTARG" ;;
    r) ROI="$OPTARG" ;;
    B) BITS="$OPTARG" ;;
    o) OUTDIR="$OPTARG" ;;
    h) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "bad option; -h for help" >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
EXTRA=("$@")

[ -x "$BIN_EXE" ] || { echo "zwo_benchmark not built at $BIN_EXE (run make)" >&2; exit 1; }
if [ -z "$OUTDIR" ]; then
  OUTDIR="./sync_$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$OUTDIR"

CSV_A="$OUTDIR/camA_${HOST_A}.csv"
CSV_B="$OUTDIR/camB_${HOST_B}.csv"

common=(--duration "$DUR" --warmup 2 --exptimes "$EXP" --bins "$BIN"
        --bits "$BITS" --rois "$ROI")

echo "capturing ${DUR}s  A=$HOST_A  B=$HOST_B  exp=${EXP}s bin=$BIN roi=${ROI}% ${BITS}bit"
echo "output: $OUTDIR"

# Launch both as close to simultaneously as possible; they self-align via
# the flash cross-correlation, so a few ms of launch skew is irrelevant.
"$BIN_EXE" --host "$HOST_A" --label camA --frame-log "$CSV_A" \
    "${common[@]}" "${EXTRA[@]}" > "$OUTDIR/camA.stdout" 2> "$OUTDIR/camA.stderr" &
PID_A=$!
"$BIN_EXE" --host "$HOST_B" --label camB --frame-log "$CSV_B" \
    "${common[@]}" "${EXTRA[@]}" > "$OUTDIR/camB.stdout" 2> "$OUTDIR/camB.stderr" &
PID_B=$!

rc=0
wait "$PID_A" || { echo "camera A ($HOST_A) failed; see $OUTDIR/camA.stderr" >&2; rc=1; }
wait "$PID_B" || { echo "camera B ($HOST_B) failed; see $OUTDIR/camB.stderr" >&2; rc=1; }

echo
echo "frames A: $(( $(wc -l < "$CSV_A") - 1 ))   B: $(( $(wc -l < "$CSV_B") - 1 ))"
echo "analyze: python3 $(dirname "$0")/plot_two_camera_sync.py $CSV_A $CSV_B"
exit $rc
