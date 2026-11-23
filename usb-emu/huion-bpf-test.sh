#!/bin/bash
# End-to-end check of a Huion HID-BPF program that reads the firmware ID
# and switches the tablet to vendor mode from the kernel, via
# hid_bpf_get_string_descriptor(). Runs inside the virtme-ng guest:
#
#   /src/tablet-devenv/vm.sh "/src/tablet-devenv/usb-emu/huion-bpf-test.sh \
#       <description> <script> <bpf.o> [udev-hid-bpf binary]"
#
# Unlike huion-switcher, nothing in userspace reads a string descriptor
# here: udev-hid-bpf only loads the program.
set -u
HERE=$(dirname "$(readlink -f "$0")")
DESC=${1:?usage: huion-bpf-test.sh <description> <script> <bpf.o> [udev-hid-bpf]}
SCRIPT=${2:?missing script}
BPF=${3:?missing bpf.o}
LOADER=${4:-/src/_build/uhb-all/udev-hid-bpf}
EMU=$HERE/usb-emu
OUT=${OUT:-/tmp/usb-emu-test}
mkdir -p "$OUT"
rm -f "$OUT/ready"
[ -x "$EMU" ] || make -s -C "$HERE" >/dev/null || exit 1

VID=$(sed -n 's/^device .*vid=0x\([0-9a-fA-F]*\).*/\1/p' "$DESC" | tr a-f A-F)
PID=$(sed -n 's/^device .*pid=0x\([0-9a-fA-F]*\).*/\1/p' "$DESC" | tr a-f A-F)
vid=$(echo "$VID" | tr A-F a-f); pid=$(echo "$PID" | tr A-F a-f)

dmesg -C
"$EMU" -v "$DESC" "$SCRIPT" 2> "$OUT/emu.log" &
EMU_PID=$!

hid_devs() { ls -d /sys/bus/hid/devices/*:"$VID":"$PID".* 2>/dev/null; }
for _ in $(seq 100); do [ "$(hid_devs | wc -l)" -ge 3 ] && break; sleep 0.2; done
sleep 1
echo "===== before BPF: firmware ID reads seen by the device"
grep -c "STRING index=20[01]" "$OUT/emu.log" | sed 's/^/string 200\/201 reads: /'

echo "===== loading $(basename "$BPF")"
# One invocation for all interfaces: a second 'udev-hid-bpf add' while a
# program is already pinned currently crashes udev-hid-bpf (independent of
# this program, also seen with the unported one).
"$LOADER" --verbose add $(hid_devs) - "$BPF" 2>&1 | grep -E "INFO|WARN|ERROR"
# attaching an rdesc fixup makes the kernel re-probe the devices
sleep 3

# input devices of our VID:PID, one paragraph each
input_devs() {
	python3 -I -c '
import sys
vid, pid = sys.argv[1:3]
for block in open("/proc/bus/input/devices").read().split("\n\n"):
    if f"Vendor={vid} Product={pid}" in block:
        print(block + "\n")' "$vid" "$pid"
}
nodes=$(input_devs | sed -n 's/^H: Handlers=//p' | tr ' ' '\n' | grep '^event')
for n in $nodes; do
	timeout 15 evtest "/dev/input/$n" > "$OUT/$n.evtest" 2>&1 &
done
sleep 0.5
touch "$OUT/ready"
for _ in $(seq 200); do grep -q "SCRIPT done\|usb-emu: " "$OUT/emu.log" && break; sleep 0.1; done
sleep 1

echo
echo "===== emulator: string descriptor reads, mode switches, script"
grep -E "STRING|MODE|SCRIPT|STALL|usb-emu: " "$OUT/emu.log" | sed 's/^usb-emu \[[^]]*\] //'
echo
echo "===== HID devices"
for d in $(hid_devs); do
	echo "$(basename "$d") driver=$(basename "$(readlink "$d/driver" 2>/dev/null)") rdesc=$(stat -c %s "$d/report_descriptor") bytes hid_uniq=\"$(sed -n 's/^HID_UNIQ=//p' "$d/uevent")\""
done
echo
echo "===== /proc/bus/input/devices"
input_devs | grep -E '^(N|U|B: (PROP|EV|KEY|ABS|REL))|^$'
echo
echo "===== events"
for n in $nodes; do
	echo "--- $n: $(sed -n 's/^Input device name: //p' "$OUT/$n.evtest")"
	grep -E "^Event: .*(EV_KEY|EV_ABS|EV_REL)" "$OUT/$n.evtest" | sed 's/^Event: time [0-9.]*, //' | head -24
done
echo
echo "===== dmesg (hid-bpf / verifier)"
dmesg | grep -iE "bpf|verifier|hid-generic" | sed 's/^\[[^]]*\] //' | head -20
kill "$EMU_PID" 2>/dev/null
"$LOADER" remove $(hid_devs) >/dev/null 2>&1
