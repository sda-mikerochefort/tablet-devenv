# usb-emu: USB HID tablet emulator (raw-gadget + dummy_hcd)

`uhid` devices aren't USB devices, so the USB-only paths of the tablet stack can't be tested with
`uhid` or hid-tools replay. That covers:
* `hid-uclogic` probing via string descriptors 100, 123, 200 and 201
* the HID-BPF string-descriptor kfunc (Phase 1)
* `usbhid` itself

`usb-emu` emulates a real USB device in the test VM: Linux raw-gadget drives dummy_hcd's device side,
dummy_hcd's host side enumerates it, and `usbhid` binds it. Every control request the host
sends is logged, so tests can assert for example that "the host read string 200 with langid 0x0409".

Kernel requirements (already in `../kernel-tablet.config`): `CONFIG_USB_GADGET=y`,
`CONFIG_USB_DUMMY_HCD=y`, `CONFIG_USB_RAW_GADGET=y`.

## Running

```sh
# in the container; KDIR picks the kernel tree that vm.sh boots
make -C /src/tablet-devenv/usb-emu
KDIR=/src/linux-emu /src/tablet-devenv/vm.sh \
  "/src/tablet-devenv/usb-emu/run-test.sh /src/tablet-devenv/usb-emu/devices/huion-hs610.desc \
                                          /src/tablet-devenv/usb-emu/devices/hs610-demo.script"
```

`run-test.sh` starts the emulator, waits for the input devices, and opens every event node with
`evtest`. usbhid only polls interrupt endpoints while the device is open, so this is what lets
replayed reports flow. It then prints:
* the dmesg lines
* the device's `/proc/bus/input/devices` entries
* HID drivers and `uniq`
* the string-descriptor reads and mode switches seen by the emulator
* the recorded events

Full logs go to `/tmp/usb-emu-test/` in the guest.

Directly: `usb-emu [-v] <description> [script|-]`. The emulator runs until killed.

## Description format

Line based. `#` starts a comment. Numbers accept `0x` hex.

```
device vid=0x256c pid=0x006d bcd=0x0100 usb=0x0200 attrs=0xa0 power=480
string 1 "HUION"                         # text, sent as UTF-16LE descriptor
string 201 "HUION_T176_190514"
string-raw 200 13 03 70 c6 00 06 ...     # sent verbatim (incl. bLength/bDescriptorType)
on-read 200 mode raw                     # reading string 200 switches the mode to "raw"
interface 0 class=3 subclass=1 protocol=2 ep=0x81 [out=0x01] maxpacket=64 interval=2
rdesc 0 06 00 ff 09 01 ...               # report descriptor; repeat to append
```

* String index 0 always returns the language table (0x0409). Any other string index or language
  ID gets the configured string, or a STALL if undefined, as a real device would. The language
  ID is logged.
* `bMaxPacketSize0` is always 64, because dummy_hcd's ep0 is 64 bytes. The device enumerates at
  high speed.
* An `out=` endpoint is advertised but not serviced.
* HID `SET_IDLE`/`SET_PROTOCOL`/`SET_REPORT` are acknowledged (SET_REPORT data is logged).
  `GET_REPORT` stalls.

## Script format

```
wait-mode raw [timeout_ms]   # block until the host triggered the mode switch
wait-file <path> [timeout_ms] # block until <path> exists (run-test.sh creates
                             # /tmp/usb-emu-test/ready once the event nodes are open)
send <iface> <hex bytes>     # one interrupt IN report (blocks until the host polls)
sleep <ms>
log <text>
```

The script starts once the host has set the configuration.

## Provided devices

| File | Device | Driver path |
|---|---|---|
| `devices/huion-hs610.desc` (+ `hs610-demo.script`) | Huion HS610, 256c:006d | `hid-uclogic` v2 path: pen + frame buttons + touch ring + dial |
| `devices/huion-inspiroy-2m.desc` | Huion Inspiroy 2 M, 256c:0067 | hid-generic + udev-hid-bpf `0010-Huion__Inspiroy-2-M` |

Provenance is noted at the top of each file. The Inspiroy 2 M interface 0 descriptor and
the illustrative firmware IDs are **reconstructed**, not captured. Replace them with real captures
when available.

## Limitations

* There's no per-mode report descriptor or interface set. The mode only gates the replay script,
  which is enough because Huion devices keep their descriptors and change only which reports they send.
* Only interrupt IN endpoints are serviced. Output reports go nowhere.
* Suspend/resume and disconnect are logged but not scripted.
