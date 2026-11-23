#!/bin/bash
# End-to-end check of a usb-emu description inside the virtme-ng guest:
#   /src/tablet-devenv/vm.sh \
#       "/src/tablet-devenv/usb-emu/run-test.sh <description> [script]"
# Needs a kernel with CONFIG_USB_DUMMY_HCD=y and CONFIG_USB_RAW_GADGET=y.
set -u
HERE=$(dirname "$(readlink -f "$0")")
DESC=${1:?usage: run-test.sh <description> [script]}
SCRIPT=${2:-}
EMU=$HERE/usb-emu
OUT=${OUT:-/tmp/usb-emu-test}
mkdir -p "$OUT"
rm -f "$OUT/ready"
[ -x "$EMU" ] || make -s -C "$HERE" >/dev/null || exit 1
[ -e /dev/raw-gadget ] || { echo "no /dev/raw-gadget in this kernel"; exit 1; }

VID=$(sed -n 's/^device .*vid=0x\([0-9a-fA-F]*\).*/\1/p' "$DESC" | tr A-F a-f)
PID=$(sed -n 's/^device .*pid=0x\([0-9a-fA-F]*\).*/\1/p' "$DESC" | tr A-F a-f)

dmesg -C
"$EMU" -v "$DESC" ${SCRIPT:+"$SCRIPT"} 2> "$OUT/emu.log" &
EMU_PID=$!

# Wait for the input devices to appear and settle.
for _ in $(seq 100); do
	grep -qi "Vendor=$VID Product=$PID" /proc/bus/input/devices && break
	sleep 0.2
done
sleep 2

nodes=$(awk -v v="$VID" -v p="$PID" '
	/^I:/ { m = (tolower($0) ~ "vendor=" v " product=" p) }
	m && /^H:/ { sub(/Handlers=/, ""); for (i = 2; i <= NF; i++) if ($i ~ /^event/) print $i }' \
	/proc/bus/input/devices)

# Opening the event nodes makes usbhid poll the interrupt endpoints, which
# is what lets the replay script's writes complete. evtest also records.
for n in $nodes; do
	timeout 15 evtest "/dev/input/$n" > "$OUT/$n.evtest" 2>&1 &
done
sleep 0.5
touch "$OUT/ready"   # scripts can 'wait-file /tmp/usb-emu-test/ready'
if [ -n "$SCRIPT" ]; then
	for _ in $(seq 150); do
		grep -q "SCRIPT done\|usb-emu: " "$OUT/emu.log" && break
		sleep 0.1
	done
fi
sleep 1

echo "===== dmesg"
dmesg | grep -iE "usb [0-9-]+:|hid|uclogic|input:" | sed 's/^\[[^]]*\] //'
echo
echo "===== /proc/bus/input/devices ($VID:$PID)"
awk -v v="$VID" -v p="$PID" '
	/^I:/ { m = (tolower($0) ~ "vendor=" v " product=" p) }
	m' RS= ORS='\n\n' /proc/bus/input/devices
echo "===== HID devices"
for d in /sys/bus/hid/devices/*; do
	case "$(basename "$d" | tr A-F a-f)" in *:"$VID":"$PID".*) ;; *) continue;; esac
	echo "$(basename "$d") driver=$(basename "$(readlink "$d/driver" 2>/dev/null)") uniq=\"$(cat "$d"/input/input*/uniq 2>/dev/null | head -1)\" rdesc=$(stat -c %s "$d/report_descriptor") bytes"
done
echo
echo "===== emulator: string descriptor reads, mode switches, script"
grep -E "STRING|MODE|SCRIPT|SEND .*fail|STALL|usb-emu: " "$OUT/emu.log" | sed 's/^usb-emu \[[^]]*\] //'
if [ -n "$SCRIPT" ]; then
	echo
	echo "===== events (evtest)"
	for n in $nodes; do
		echo "--- $n: $(sed -n 's/^Input device name: //p' "$OUT/$n.evtest")"
		grep -E "^Event: .*(EV_KEY|EV_ABS|EV_REL|EV_MSC)" "$OUT/$n.evtest" | sed 's/^Event: time [0-9.]*, //' | head -20
	done
fi
kill "$EMU_PID" 2>/dev/null
echo
echo "(full logs in $OUT)"
