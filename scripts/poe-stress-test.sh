#!/bin/bash
#
# Stress a Raspberry Pi at (roughly) its maximum power draw and report whether
# the power path -- in our case a PoE HAT -- held 5 V for the whole run.
#
# Written for the guider hosts: a Pi 4 on a Waveshare PoE HAT (C), an
# 802.3af/at HAT rated 5 V / 4.5 A.  A Pi 4 pulls ~1.2 A with every core in
# fft, so on paper there is headroom; what this test finds is a HAT, cable
# or switch port that sags under a sustained load rather than one that
# cannot deliver it at all.  It runs on a Pi 5 too (adds the PMIC's EXT5V
# reading); on a Pi 4 that column is "-".
#
# This is the usual community recipe for qualifying a Pi power supply:
# stress-ng's fft cpu method on every core is the heaviest CPU load people
# have measured (more than cpuburn), a vm stressor adds DRAM traffic, and
# optional network / disk load covers the extras a guider host actually
# powers (the ZWO camera on USB, the NIC at full tilt).  While it runs we
# watch the three signals the firmware gives us:
#
#   * vcgencmd get_throttled  -- bit 0 "under-voltage now", bit 16 "has
#                                occurred since boot" (sticky), bits 1-3 /
#                                17-19 frequency capping and throttling;
#   * the rpi_volt hwmon      -- in0_lcrit_alarm goes 1 while under-voltage;
#   * the kernel log          -- "hwmon hwmonN: Undervoltage detected!" and
#                                "Voltage normalised" are what dmesg shows.
#
# On a Pi 5 the PMIC ADC is read as well (EXT5V_V is the rail the HAT feeds).
#
# The other failure mode is the one you cannot log: with a HAT that cannot
# keep up the Pi simply browns out and reboots a few seconds into the load.
# A marker file records the boot id at start; if the script is run again and
# the boot id changed while the marker was still there, it says so.
#
# The acceptance procedure this is part of (fan, 12 V header, labelling) is
# in docs/ZWO/poe_hat_testing.md.
#
#   usage: sudo poe-stress-test.sh [-t DURATION] [-i SECS] [-n IPERF_HOST]
#                                  [-d DIR] [-o LOGDIR]
#
#     -t  stress-ng --timeout (default 10m)
#     -i  sampling interval in seconds (default 5)
#     -n  also run an iperf3 client against this host for the duration
#     -d  also hammer a disk with stress-ng --hdd in this directory
#         (point it at a USB-attached disk to load the USB rail)
#     -o  where to write the CSV of samples and the summary (default /var/tmp)
#
# Exit status: 0 when no under-voltage was seen, 1 when it was, 2 on a
# usage / environment problem.  Run it as root so the kernel log is readable.

set -u

DURATION=10m
INTERVAL=5
IPERF_HOST=
HDD_DIR=
LOGDIR=/var/tmp

while getopts "t:i:n:d:o:h" opt; do
    case $opt in
        t) DURATION=$OPTARG ;;
        i) INTERVAL=$OPTARG ;;
        n) IPERF_HOST=$OPTARG ;;
        d) HDD_DIR=$OPTARG ;;
        o) LOGDIR=$OPTARG ;;
        *) sed -n '/^#   usage/,/^#$/p' "$0" | cut -c3-; exit 2 ;;
    esac
done

die() { echo "poe-stress-test: $*" >&2; exit 2; }

command -v stress-ng >/dev/null || die "stress-ng not installed (apt install stress-ng)"
command -v vcgencmd  >/dev/null || die "vcgencmd not found -- is this a Raspberry Pi?"
[ "$(id -u)" -eq 0 ] || echo "warning: not root; kernel log may be unreadable" >&2
[ -n "$IPERF_HOST" ] && { command -v iperf3 >/dev/null || die "iperf3 not installed"; }
[ -n "$HDD_DIR" ] && { [ -d "$HDD_DIR" ] || die "$HDD_DIR is not a directory"; }
mkdir -p "$LOGDIR" || die "cannot create $LOGDIR"

STAMP=$(date +%Y%m%d-%H%M%S)
CSV=$LOGDIR/poe-stress-$STAMP.csv
SUMMARY=$LOGDIR/poe-stress-$STAMP.txt
MARKER=$LOGDIR/poe-stress.running
BOOT_ID=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)

# ---- helpers -------------------------------------------------------------

throttled() { vcgencmd get_throttled | cut -d= -f2; }

decode_throttled() {
    # print the set bits of a get_throttled value, one per line
    local v=$(( $1 ))
    local bit desc
    for bit in 0:"under-voltage NOW" 1:"ARM frequency capped NOW" \
               2:"throttled NOW" 3:"soft temperature limit NOW" \
               16:"under-voltage has occurred" 17:"ARM frequency capping has occurred" \
               18:"throttling has occurred" 19:"soft temperature limit has occurred"; do
        desc=${bit#*:}; bit=${bit%%:*}
        [ $(( v & (1 << bit) )) -ne 0 ] && echo "    bit $bit: $desc"
    done
    return 0
}

# the rpi_volt hwmon: in0_lcrit_alarm is 1 while the firmware sees < ~4.63 V
HWMON_ALARM=
for h in /sys/class/hwmon/hwmon*; do
    [ "$(cat "$h/name" 2>/dev/null)" = rpi_volt ] && HWMON_ALARM=$h/in0_lcrit_alarm && break
done
alarm() { [ -n "$HWMON_ALARM" ] && cat "$HWMON_ALARM" 2>/dev/null || echo "-"; }

temp()    { vcgencmd measure_temp | sed 's/temp=//; s/.C$//'; }
armclk()  { echo $(( $(vcgencmd measure_clock arm | cut -d= -f2) / 1000000 )); }
corev()   { vcgencmd measure_volts core | sed 's/volt=//; s/V$//'; }

# Pi 5: the PMIC ADC reports the 5 V input rail directly
HAVE_PMIC=0
vcgencmd pmic_read_adc >/dev/null 2>&1 && HAVE_PMIC=1
ext5v() {
    [ $HAVE_PMIC -eq 1 ] || { echo "-"; return; }
    vcgencmd pmic_read_adc EXT5V_V 2>/dev/null | sed -n 's/.*=\([0-9.]*\)V.*/\1/p'
}

kernel_log_since() {
    # under-voltage lines from the kernel ring since $1 (a date string)
    if command -v journalctl >/dev/null; then
        journalctl -k -q --since "$1" 2>/dev/null
    else
        dmesg -T 2>/dev/null
    fi | grep -i -E 'under-?voltage|voltage normalised'
}

# ---- preflight -----------------------------------------------------------

{
    echo "== poe-stress-test $STAMP"
    echo "model:     $(cat /proc/device-tree/model 2>/dev/null | tr -d '\0')"
    echo "kernel:    $(uname -r)"
    echo "firmware:  $(vcgencmd version | head -1)"
    echo "uptime:    $(uptime -p)"
    echo "cpus:      $(nproc)"
    echo "duration:  $DURATION   interval: ${INTERVAL}s"
    [ -n "$IPERF_HOST" ] && echo "iperf3:    -> $IPERF_HOST"
    [ -n "$HDD_DIR" ]    && echo "hdd:       $HDD_DIR"
    echo "hwmon:     ${HWMON_ALARM:-none (kernel < 4.14?)}"
    echo "pmic adc:  $([ $HAVE_PMIC -eq 1 ] && echo yes || echo no)"
    # Pi 5 only: USB is capped at 600 mA unless config.txt says the supply
    # can do 5 A -- matters for a camera on USB; prints nothing on a Pi 4
    umc=$(vcgencmd get_config usb_max_current_enable 2>/dev/null)
    case $umc in ""|*unknown*) ;; *) echo "config:    $umc" ;; esac
    echo
    echo "throttled before: $(throttled)"
    decode_throttled "$(throttled)"
    if [ -f "$MARKER" ]; then
        if [ "$(cat "$MARKER")" != "$BOOT_ID" ]; then
            echo
            echo "!! a previous run left its marker and the boot id has changed:"
            echo "!! the Pi REBOOTED during that test -- brown-out is the likely cause"
        else
            echo "note: stale marker from an interrupted run this boot"
        fi
    fi
    echo
} | tee "$SUMMARY"

echo "$BOOT_ID" > "$MARKER"
START=$(date '+%Y-%m-%d %H:%M:%S')
T0_THROTTLED=$(throttled)

# ---- load ----------------------------------------------------------------

STRESS_ARGS=(--cpu "$(nproc)" --cpu-method fft
             --vm 1 --vm-bytes 25% --vm-method all
             --timeout "$DURATION" --metrics-brief --log-file "$LOGDIR/poe-stress-$STAMP.stress-ng.log")
[ -n "$HDD_DIR" ] && STRESS_ARGS+=(--hdd 1 --hdd-bytes 512M --temp-path "$HDD_DIR")

echo "starting: stress-ng ${STRESS_ARGS[*]}" | tee -a "$SUMMARY"
stress-ng "${STRESS_ARGS[@]}" >/dev/null 2>&1 &
STRESS_PID=$!

IPERF_PID=
if [ -n "$IPERF_HOST" ]; then
    # DURATION is a stress-ng spec (10m, 600s, 600); iperf wants seconds
    secs=$DURATION
    case $secs in
        *h) secs=$(( ${secs%h} * 3600 )) ;;
        *m) secs=$(( ${secs%m} * 60 )) ;;
        *s) secs=${secs%s} ;;
    esac
    iperf3 -c "$IPERF_HOST" -t "$secs" -P 4 > "$LOGDIR/poe-stress-$STAMP.iperf3.log" 2>&1 &
    IPERF_PID=$!
fi

cleanup() {
    kill "$STRESS_PID" ${IPERF_PID:+"$IPERF_PID"} 2>/dev/null
    wait 2>/dev/null
    rm -f "$MARKER"
}
trap cleanup EXIT INT TERM

# ---- sample --------------------------------------------------------------

echo "time,elapsed_s,temp_C,arm_MHz,core_V,ext5v_V,hwmon_alarm,throttled" > "$CSV"
printf '%-8s %6s %7s %8s %7s %7s %5s %s\n' time elaps temp_C arm_MHz core_V ext5v alarm throttled
t0=$(date +%s)
UV_SAMPLES=0
ALARM_SAMPLES=0
CAP_SAMPLES=0
while kill -0 "$STRESS_PID" 2>/dev/null; do
    now=$(date +%s)
    th=$(throttled); a=$(alarm); tc=$(temp); mhz=$(armclk); cv=$(corev); e5=$(ext5v)
    flag=""
    [ $(( th & 1 )) -ne 0 ] && { UV_SAMPLES=$((UV_SAMPLES+1)); flag="  <-- UNDER-VOLTAGE"; }
    [ "$a" = 1 ] && ALARM_SAMPLES=$((ALARM_SAMPLES+1))
    [ $(( th & 0x6 )) -ne 0 ] && CAP_SAMPLES=$((CAP_SAMPLES+1))
    echo "$(date +%H:%M:%S),$((now-t0)),$tc,$mhz,$cv,$e5,$a,$th" >> "$CSV"
    printf '%-8s %6d %7s %8s %7s %7s %5s %s%s\n' "$(date +%H:%M:%S)" $((now-t0)) "$tc" "$mhz" "$cv" "$e5" "$a" "$th" "$flag"
    sleep "$INTERVAL"
done
wait "$STRESS_PID"
STRESS_RC=$?
[ -n "$IPERF_PID" ] && wait "$IPERF_PID"
trap - EXIT INT TERM
rm -f "$MARKER"

# ---- verdict -------------------------------------------------------------

T1_THROTTLED=$(throttled)
NEW_STICKY=$(( (T1_THROTTLED & ~T0_THROTTLED) & 0xF0000 ))
LOGLINES=$(kernel_log_since "$START")
NLOG=$(printf '%s' "$LOGLINES" | grep -c 'ndervoltage detected' || true)

if [ "$UV_SAMPLES" -gt 0 ] || [ "$ALARM_SAMPLES" -gt 0 ] || [ "$NLOG" -gt 0 ] \
   || [ $(( NEW_STICKY & 0x10000 )) -ne 0 ]; then
    VERDICT_TEXT="NOT PASS: under-voltage seen during the run -- the power path did not hold 5 V"
    RC=1
elif [ $(( NEW_STICKY & 0x40000 )) -ne 0 ] || [ "$CAP_SAMPLES" -gt 0 ]; then
    VERDICT_TEXT="PASS (power): no under-voltage, but the CPU was throttled -- that is thermal,
look at the peak temperature and the HAT's fan"
    RC=0
else
    VERDICT_TEXT="PASS: no under-voltage, no throttling"
    RC=0
fi

{
    echo
    echo "== result"
    echo "stress-ng exit:      $STRESS_RC"
    echo "samples:             $(( $(wc -l < "$CSV") - 1 ))  (csv: $CSV)"
    echo "peak temp:           $(cut -d, -f3 "$CSV" | tail -n +2 | sort -n | tail -1) C"
    # skip the first sample: it is taken before stress-ng has ramped the clock up
    echo "min arm clock:       $(cut -d, -f4 "$CSV" | tail -n +3 | sort -n | head -1) MHz  (after warm-up)"
    [ $HAVE_PMIC -eq 1 ] && echo "min EXT5V:           $(cut -d, -f6 "$CSV" | tail -n +2 | sort -n | head -1) V"
    echo "under-voltage samples (get_throttled bit 0): $UV_SAMPLES"
    echo "hwmon alarm samples:                          $ALARM_SAMPLES"
    echo "capped/throttled samples:                     $CAP_SAMPLES"
    echo "kernel 'Undervoltage detected' events:        $NLOG"
    echo "throttled after:     $T1_THROTTLED"
    decode_throttled "$T1_THROTTLED"
    if [ -n "$LOGLINES" ]; then
        echo
        echo "kernel log since $START:"
        printf '%s\n' "$LOGLINES" | sed 's/^/    /'
    fi
    echo
    echo "$VERDICT_TEXT"
    echo "summary: $SUMMARY"
} | tee -a "$SUMMARY"

# the banner: green PASS / red NOT PASS, colour only when stdout is a terminal
if [ -t 1 ]; then GREEN=$'\e[1;32m'; RED=$'\e[1;31m'; OFF=$'\e[0m'; else GREEN='' RED='' OFF=''; fi
echo
if [ "$RC" -eq 0 ]; then
    echo "${GREEN}=====  PASS  =====${OFF}"
else
    echo "${RED}===== NOT PASS =====${OFF}"
fi
echo

exit "$RC"
