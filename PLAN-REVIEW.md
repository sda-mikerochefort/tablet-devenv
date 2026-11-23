# Plan review: corrections and deltas against the current upstream trees

These checks were made against the dev-branch bases: linux v7.4-rc4, libinput 1.32+,
libwacom 2.20+, wayland-protocols 1.50, udev-hid-bpf main, and systemd main `input_id`.
Most of the plan holds up. The items below change scope or correct a spec detail.

## Tier 1: kernel

1. **`hid_bpf_set_uniq()` is unnecessary.** Since v6.12 (commit 33c0fb85b571, "HID: bpf: make
   part of struct hid_device writable"), `hid_bpf_ops` programs can write `hdev->name`, `->uniq` and
   `->phys` directly (`drivers/hid/bpf/hid_bpf_struct_ops.c`, `WRITE_RANGE`). The only new kernel
   API actually needed is `hid_bpf_get_string_descriptor()`, plus the `hid_ll_driver` transport hook.
   * Timing: udev-hid-bpf runs `probe()` (a `SEC("syscall")` program) from userspace *after* the device
     is bound. A changed `uniq` reaches userspace only through the re-probe that attaching an
     `rdesc_fixup` triggers. The spec should say this explicitly.
   * Prior art for the `usbhid` side: `uclogic_params_get_str_desc()` in
     `drivers/hid/hid-uclogic-params.c` already reads string descriptors 100 and 200 (0x64/0xC8) with
     `usb_control_msg()`. Factor it out instead of writing new code.
   * udev-hid-bpf can already hand data to userspace through `EXPORT_UDEV_PROP()` (see the
     `HUION_FIRMWARE_ID` handling in `0010-Huion__Inspiroy-2-M.bpf.c`).
2. **Canonical evdev contract table (§1.2) has two errors.**
   * Strips are `ABS_RX` (strip 1) and `ABS_RY` (strip 2). `ABS_THROTTLE` is **ring 2**.
     See `libinput/src/evdev-tablet-pad.c` (pad_handle_ring/pad_handle_strip).
   * "Pad identified via `BTN_STYLUS` + `BTN_0` *without* `ABS_X/Y`" is wrong. systemd's
     `udev-builtin-input_id.c` sets `ID_INPUT_TABLET_PAD` only when the device has (`ABS_X`+`ABS_Y`+`BTN_STYLUS`
     +`BTN_0`+`BTN_1`, no `BTN_TOOL_PEN`) **or** (`BTN_0`+`BTN_1` + `REL_WHEEL`/`REL_HWHEEL`, no
     `REL_X/Y`). Existing BPF fixups add dummy X/Y for exactly this reason ("just exists so we get to be
     a tablet pad"). Either keep the dummy-axis convention in the contract, or add a systemd change
     to Phase 1.
   * Dials: libinput accepts `REL_WHEEL[_HI_RES]`/`REL_HWHEEL[_HI_RES]` **or** `REL_DIAL`, and treats
     `REL_DIAL`+`REL_WHEEL` together as a bug.
3. **Mode LEDs already use the LED class.** wacom registers `"%s::wacom-%d.%d"` LED class devices
   (`wacom_sys.c`), and libinput already drives them (`evdev-tablet-pad-leds.c`). There is no
   `selector` function in `include/dt-bindings/leds/common.h`, so the work is to agree on a vendor-neutral
   function name (`LED_FUNCTION_INDICATOR`/`STATUS` or a new one) and to teach libinput both names.
   It is not a new mechanism. `wacom_led/` sysfs files are ABI and can only be deprecated, not removed.
4. **HIDIOCREVOKE**: confirmed in v6.12-rc1 (b31c9d9dc343). Correct as written.

## Tier 2: libtablet / libinput

5. **libwacom soname is `libwacom.so.9`** (libwacom 2.x), not `.so.2`. The compat shim must
   provide `libwacom.so.9` and `libwacom.pc`.
6. **Most of the ".tablet v2" schema already exists.** `DeviceMatch=bus|vid|pid|name|uniq` (for
   example `huion-*.tablet` with `HUION_T166`), `libwacom_match_get_uniq()`, and `NumDials=` /
   `Dial=` / `DialNumModes=` (`data/wacom.example`) are all in place. The genuinely new parts are
   `DialDetentsPerRev=`, the SVG dial leader-lines, and the `[OLED]` section.
7. **libinput already has**:
   * rectangle-based ("zonal") touch arbitration: `ARBITRATION_IGNORE_RECT` and
     `tablet_calculate_arbitration_rect()` in `evdev-tablet.c`. The three-mode state machine
     extends this; it doesn't replace it.
   * a Lua plugin system (`plugins/*.lua`, meson option `lua-plugins`). Fedora 45 ships Lua 5.5.
   * `LIBINPUT_DEVICE_GROUP` through `udev/libinput-device-group.c` (vid/pid + phys). The
     `ID_CONTAINER`-style grouping across interfaces is the delta.

## Tier 3: wayland-protocols

8. In 1.50, `stable/tablet/tablet-v2.xml` is still **version 2**, with dials (`zwp_tablet_pad_dial_v2`)
   and `bustype` at `since="2"`. A v3 bump is the right vehicle.

## Scope / missing repositories

The plan touches projects that have no repository in this environment: **systemd**
(udev input_id, logind), **KWin**, **wlroots**, **Smithay**, **Qt/GTK/Krita/Blender**, **fwupd**,
and **wacom-hid-descriptors** (`tablet-hid-descriptors`). If they are needed, mirror them the same way.
