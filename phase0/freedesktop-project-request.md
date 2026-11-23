# Draft: freedesktop.org GitLab project request

To be filed by a human as an issue in the freedesktop.org admin tracker
(`gitlab.freedesktop.org/freedesktop/freedesktop`), using that tracker's "new project" template.
File it only **after** the linuxwacom maintainers have agreed (see `governance.md`).
Fill in the `<...>` fields.

---

**Title:** New group: `libtablet` (vendor-neutral tablet metadata, successor of linuxwacom/libwacom)

**Requested group:** `libtablet`

**Projects:**

* `libtablet/libtablet`: imported with full history from `github.com/linuxwacom/libwacom`.
  A C library plus `.tablet`/`.stylus`/SVG data describing graphics tablets, pen displays and
  remotes. It keeps providing `libwacom.so.9`/`libwacom.pc` for compatibility. License: HPND.
* `libtablet/tablet-hid-descriptors`: imported with full history from
  `github.com/linuxwacom/wacom-hid-descriptors`. HID report descriptors and recordings used for
  kernel/HID-BPF/libinput regression testing.

**Why freedesktop.org:** the projects serve all tablet vendors (Wacom, Huion, XP-Pen, Xencelabs,
UGEE/Gaomon) and their consumers on freedesktop.org already: libinput, udev-hid-bpf, hid-tools,
Wayland/wayland-protocols, Mutter and KWin. A vendor-neutral home lets vendors contribute as equals.

**Initial maintainers:** `<current libwacom maintainers' fd.o usernames>`, `<community maintainer(s)>`

**Agreement from current maintainers:** `<link to the discussion or issue on linuxwacom/libwacom>`

**CI needs:** the standard fd.o shared runners, using `ci-templates` like libinput and udev-hid-bpf.
No special hardware.

**Mailing list / chat:** existing `linuxwacom-devel` traffic moves to GitLab issues. Optional Matrix room `#libtablet:<homeserver>`.
