# libtablet: vendor-neutral governance proposal (draft)

Status: draft for discussion with the current linuxwacom maintainers before anything public.
Nothing here has been sent or requested.

## Goal

Give Wacom, Huion, XP-Pen, Xencelabs, Gaomon/UGEE and community projects (OpenTabletDriver, the
DIGImend contributors) a neutral place to contribute tablet metadata and test data as equals.
The code and data already exist in `linuxwacom/libwacom` and `linuxwacom/wacom-hid-descriptors`.
The change is about **ownership and branding**, not a rewrite.

## Proposed structure

| Project | Source | Home |
|---|---|---|
| `libtablet` | `linuxwacom/libwacom` (full history) | `gitlab.freedesktop.org/libtablet/libtablet` |
| `tablet-hid-descriptors` | `linuxwacom/wacom-hid-descriptors` (full history) | `gitlab.freedesktop.org/libtablet/tablet-hid-descriptors` |
| XDG tablet profile spec | new (`xdg-tablet/` in libtablet) | stays in libtablet until there are two implementations, then moves to its own repo |

* **License:** unchanged. libwacom is HPND (`COPYING`, `meson.build`). New files use the same license. No CLA.
  Contributions are under the DCO (`Signed-off-by`), as in other freedesktop projects.
* **Compatibility:** libtablet keeps shipping `libwacom.so.9`, `libwacom.pc`, `libwacom.h` and the
  `65-libwacom.{rules,hwdb}` names until all major consumers have moved (GNOME Settings/Mutter, KDE
  wacomtablet/KWin, libinput, xf86-input-wacom). The old GitHub repos are archived with a pointer.
* **Code of conduct:** the freedesktop.org Code of Conduct applies.

## Roles

* **Maintainers** (merge rights on all files): the existing libwacom maintainers carry over, plus up to
  two community maintainers. A maintainer must not be the sole approver of a change that only affects
  their own employer's hardware.
* **Vendor data stewards** (one or two per vendor): `Developer` access, but MRs touching only
  `data/<vendor>-*.tablet`, `data/layouts/<vendor>-*.svg` and `tablet-hid-descriptors/<vendor>/` may be
  merged by a steward of *that* vendor once CI passes. Changes to shared files (schema, code, other
  vendors' data) need a maintainer.
* **Decisions:** lazy consensus on the MR. Contested changes to the `.tablet` schema or the profile spec
  need approval from two maintainers employed by different organisations.

## What vendors are asked to contribute

1. A `.tablet` file and an SVG layout for every model, merged **before** the hardware ships.
2. A `tablet-hid-descriptors` capture: report descriptors and `hid-recorder` event recordings (format to
   be defined in Phase 4) for every interface and mode.
3. Unique USB product IDs per hardware revision. Where IDs are reused, a firmware ID readable from a USB
   string descriptor, documented for the HID-BPF program to check.
4. Optional: HID-BPF fixups for udev-hid-bpf and the kernel, reviewed through the normal upstream process.

## Steps (human)

1. Agree the proposal with the linuxwacom maintainers (Wacom employees and Red Hat). Their agreement is
   the gate: without it, this becomes a fork, which the plan explicitly avoids.
2. File the freedesktop.org project request (see `freedesktop-project-request.md`).
3. Mirror both repos with full history, set up CI, and archive the GitHub repos with a notice.
4. Send the vendor invitations (`vendor-invitation.md`).
