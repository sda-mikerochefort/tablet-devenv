# Phase 1: kernel HID-BPF and evdev unification

User decisions applied:
* **Dial direction:** clockwise is positive in right-handed mode and reversed in left-handed mode.
* **Mode LED names:** `<input>::tablet-<group>.<mode>`.

## Delivered (all on `claude/elegant-goldberg-5odfs6`)

| Repo | Commits | What |
|---|---|---|
| linux | `0d5358506e4d` | Contract doc: dial direction (clockwise positive; left-handed reversal is userspace policy), `::tablet-` LED names with a `::wacom-` fallback |
| linux | `0db9f58e2b7e` `fd0af41ac200` `7d6a2a692c87` `a5bf3173f825` | `hid_ll_driver.get_string_descriptor()`, `hid_hw_get_string_descriptor()`, usbhid implementation, `hid_bpf_get_string_descriptor()` kfunc (sleepable; syscall and struct_ops), docs |
| linux | `601ce1e0002b` | selftests: the kfunc on uhid (`-EOPNOTSUPP`, `-EINVAL`) from a syscall program and from a sleepable `hid_rdesc_fixup` |
| linux | `5206f95ddea6` | **Regression fix** (Fixes: 9671854582f9, v6.17): hid-uclogic remapped every keypad button. Button 10 became `BTN_RIGHT`, and buttons 11–20 collided with 1–10 and went silent. The remap now applies only to fields with more than ten buttons, so the Artist 22R Pro and 24 Pro are unchanged |
| linux | `61d3718b7c84` | hid-uclogic: `HID_QUIRK_INPUT_PER_APP`, so buttons, ring, strip and dial share one Pad node, separate from the Pen node |
| udev-hid-bpf | `04d099d` `482fbd2` `9bb1865` | 9 Huion/Gaomon programs read the firmware ID (string 201), set `uniq` and switch to native mode (string 200) in a sleepable `hid_rdesc_fixup`. They fall back to huion-switcher's udev property on older kernels |
| libinput | `60590e2a` | Dials: clockwise positive for `REL_WHEEL`, `REL_HWHEEL` and `REL_DIAL`, inverted in left-handed mode (an API behaviour change for dial 1) |
| libinput | `c197945f` | Mode LEDs: try `::tablet-` first, then fall back to `::wacom-` |
| tablet-devenv | `usb-emu/` | raw-gadget + dummy_hcd USB tablet emulator. Tests USB-only paths (string descriptors, hid-uclogic) in the VM |

## Verification

* Kernel builds; the new and changed files are clean with `W=1`. checkpatch `--strict` flags only the missing Signed-off-by, the trailer form, and one `extern` that follows the existing style of the selftest header.
* `hid_bpf` selftests: 49 pass, 1 skip, 0 fail, including the 2 new tests. HID KUnit, including uclogic: all pass.
* Emulated HS610 (hid-uclogic):
  * Before: Pen, Pad, "Touch Ring" and "Dial" nodes. Button 10 gave `BTN_RIGHT` and buttons 11–13 gave no events.
  * After: Pen and Pad only. Buttons 9–13 give `BTN_8`, `BTN_9`, `BTN_SOUTH`, `BTN_EAST`, `BTN_C`, matching libwacom.
* Emulated Inspiroy 2 M (udev-hid-bpf, no huion-switcher): the kernel reads string 201 on every interface and string 200 once. `Uniq=HUION_T21k_221221`. Only the native nodes remain, and pen and pad events flow.
* udev-hid-bpf: 64/64 new tests. The existing suite has only the 2 known `test_helpers` failures.
* libinput, in the VM:
  * The full `tablet*` groups: 1782 pass, 2094 not applicable, 0 fail.
  * The full `pad` group with `-j1`: 292 pass, 21 not applicable, 0 fail.
  * The `pad_*` tests, including the new dial tests: 310 pass, 0 fail.
  * With `-j4` the test runner intermittently segfaults before running anything. Unmodified upstream (255d4dfd) does the same (2 of 4 runs), so this is pre-existing and not caused by these changes. It should be reported upstream.

## Not done, and why

* **Mode LEDs for XP-Pen, Huion and Xencelabs.** No public protocol, and no hardware or captures. Needs vendor documentation or `hid-recorder` captures of the vendor app toggling the LEDs. The contract and libinput side are ready (`::tablet-`).
* **Wireless receiver helper and battery for 2.4 GHz dongles.** Same blocker: per-vendor receiver protocols.
* **Renaming wacom's LEDs to `::tablet-`.** libinput *reads* the LED brightness to track the active mode, so a rename would regress existing userspace. Revisit after a libinput release with the fallback is widespread.
* **Artist 22R Pro / 24 Pro button codes** (`BTN_RIGHT`, `BTN_MIDDLE`, …) still violate the contract. Fixing them needs a coordinated kernel and libwacom change.
* **Suspend/resume for Huion native mode.** After a reset-resume the tablet may return to firmware mode, and `hid_rdesc_fixup` is not re-run (huion-switcher had the same problem). Candidate kernel change: re-run the rdesc fixup, or add a resume callback to `hid_bpf_ops`.
* **Kernel copies of the BPF programs** (`drivers/hid/bpf/progs`) still use the udev property. They sync from udev-hid-bpf after the MR lands, using the upstream workflow.
* **Deprecating huion-switcher.** Do it after the kfunc reaches a released kernel. Programs already fall back to it on older kernels.

## Verify on real hardware before submitting

* **Dial direction:** that Huion/XP-Pen dials send positive `REL_WHEEL` when turned clockwise. If not, the BPF programs must invert, not libinput.
* **Firmware IDs and magic bytes:** the emulated values are reconstructed, not captured.
* **Read order:** whether every target accepts reads of string 201 before 200, on every interface.

## Bugs found along the way

* `udev-hid-bpf add`/`remove` segfaults in the VM when a program is already pinned. The unmodified Inspiroy 2 M program shows the same crash, so it's not from our changes. Not investigated yet.
* Fedora 45's clang 23 breaks five udev-hid-bpf programs and the kernel's `drivers/hid/bpf/progs`, which now build with clang 21.

## Submission notes

* Kernel: send two series to linux-input, Cc the HID maintainers (see `phase0/kernel-submission.md`):
  * the string-descriptor kfunc series, with its selftests
  * the uclogic series; the regression fix should go to stable
* No Signed-off-by has been added. A human must add it.
* libinput: its policy asks for an `Assisted-by: AI` trailer; drop `Co-Authored-By` before opening the MR. The dial change needs maintainer agreement, because it flips dial 1's sign compared with 1.32.
