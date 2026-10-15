# PoE HAT acceptance test

Every Waveshare PoE HAT (C) goes through this test before it is deployed on
a guider Raspberry Pi. The HAT is the only power source of the Pi and of the
ZWO camera hanging off its USB, so a marginal unit shows up in the dome as a
guider that reboots under load, not as a clean failure. The test takes about
20 minutes per unit and ends with a sticker on the HAT.

The HAT is an IEEE 802.3af/at device, 37-57 V in, and provides 5 V / 4 A to
the Pi plus a 12 V / 2 A auxiliary output on a 2.54 mm pin header. The fan
runs directly from the board and is not software controlled on this model.

## What you need

- The HAT under test and a Raspberry Pi 4 (the bench Pi or the target host).
- A switch port with 802.3at (PoE+) enabled and a known-good Ethernet cable.
- A multimeter with DC volts and a pair of Dupont leads for the 12 V header.
- ssh access to the Pi. The stress script needs `stress-ng`
  (`sudo apt install stress-ng`) and is in this repository at
  [scripts/poe-stress-test.sh](../../scripts/poe-stress-test.sh).
- A label and a marker.

## Procedure

### 1. Inspection and mounting

Check the board for bent header pins, loose fan wiring and damage to the
transformer. Fit the HAT on the 40-pin header and the four PoE pins of the
Pi, with the standoffs, so the fan sits over the SoC.

### 2. Unpowered resistance check of the 12 V output

Before applying power, set the multimeter to ohms and measure across the
12 V header with the **red lead on GND and the black lead on 12V**. The
polarity matters: the 12 V rail is the output of a switching converter and
reads differently with the leads swapped. Units tested in September 2026
fall into two groups, about 120 ohm and about 260 ohm, with no visible
difference between the boards; both go on to pass the powered tests. The
meter is probably reading a diode across the converter output, so the
number depends on the meter and its range as much as on the board. Use the
same meter and range every time, hold the probes until the reading settles,
and write the value down. A reading near 0 ohm means the output is shorted and an open circuit
means the output stage is broken; reject either without powering the unit.
A value outside both groups is not a failure by itself, but note it and
watch the powered 12 V readings of that unit more closely.

### 3. Power-up and fan

Connect the Ethernet cable to the PoE+ port. The Pi's red power LED must come
on and the fan must start immediately and keep running. Listen for rattling
or grinding and confirm airflow with a finger over the fan. A fan that does
not start, stalls or is noisy fails the unit: the fan is the only cooling
the Pi has inside the guider box.

### 4. 12 V output

With the HAT powered from the switch port and the Pi idle, measure the
voltage at the 12 V header (12V pin to GND) with the multimeter. Accept
11.4-12.6 V (12 V +/- 5 %). Write the value down.

### 5. Stress test

Log in to the Pi and run the stress script for the full ten minutes:

```
sudo /path/to/poe-stress-test.sh
```

It loads all four cores with stress-ng's fft method plus a memory stressor,
which is the heaviest sustained draw a Pi 4 produces, and samples the
firmware's under-voltage flag, the `rpi_volt` hwmon alarm and the kernel log
for `Undervoltage detected` while it runs. It ends with a green `PASS` or a
red `NOT PASS` banner and leaves a CSV of samples and a summary in
`/var/tmp`.

While the test runs, measure the 12 V header again; it must stay within
11.4-12.6 V under load. Keep an eye on the fan too, it has to keep running
for the whole run.

If the Pi reboots during the test the HAT could not hold 5 V; the script
reports this on its next start because it leaves a marker with the boot id.

Add `-n <iperf3 server>` to also load the network interface, and connect
the camera if you want the USB rail loaded as it will be in the dome.

### 6. Verdict

The unit passes when all of these hold:

| Check | Accept |
|---|---|
| 12 V header, unpowered | about 120 or 260 ohm, red lead on GND, black on 12V; not a short, not open |
| Fan | starts at power-up, runs through the stress test, no noise |
| 12 V idle | 11.4-12.6 V |
| 12 V under load | 11.4-12.6 V |
| Stress test | `PASS`, no reboot |

A `NOT PASS` from the script is not automatically the HAT's fault. Repeat
once with a different cable and switch port; if it passes then, the cable or
port was the problem. If it fails again, reject the HAT.

### 7. Label

Put a sticker on the top of the HAT, next to the fan, with the tester's
initials and the month of the test:

```
tested - ws
09/2026
```

Here `ws` is the tester and `09/2026` the month the unit was tested. A HAT
without the sticker has not been tested and must not be deployed.

## Record

Keep one line per unit in the table below, or in the lab notebook, together
with the summary file the script writes.

| Date | HAT | Pi / host | Switch port | 12 V ohms | 12 V idle | 12 V load | Fan | Stress | Tester |
|---|---|---|---|---|---|---|---|---|---|
|  |  |  |  |  |  |  |  |  |  |

## Background

The choice of the HAT and the power budget of the Pi plus camera are in
[ZWO Power Measurement](zwo_power_measurement.md). The stress recipe follows
what is commonly used to qualify Pi power supplies: stress-ng fft on all
cores is the highest measured CPU draw on a Pi 4, and the under-voltage
flag, hwmon alarm and kernel messages are the three signals the firmware
exposes when the 5 V rail drops below about 4.63 V.
