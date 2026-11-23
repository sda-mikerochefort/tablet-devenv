# Draft: invitation to tablet vendors

Recipients: Linux or developer-relations contacts at Wacom, Huion, XP-Pen, Xencelabs and
UGEE/Gaomon, and the OpenTabletDriver maintainers. To be sent by a human, only after the freedesktop.org
group exists. Adjust the tone per recipient.

---

**Subject:** Upstream Linux support for your tablets: an invitation to libtablet

Hello,

We're working with the Linux input community (kernel HID, libinput, Wayland and the GNOME/KDE
compositors) on a single, vendor-neutral stack for pen tablets and pen displays. The goal is that your
devices work on Linux on launch day, with no proprietary driver and no `uinput` daemon. Pressure, tilt,
ExpressKeys, dials, on-device displays, per-application profiles and screen mapping would all be handled
by components that ship with every distribution.

What already exists upstream today:

* The kernel's HID-BPF lets small, verified programs fix a device's report descriptor or switch it into
  tablet mode. These programs ship with the kernel and with `udev-hid-bpf`. Several Huion and XP-Pen
  models are supported this way already.
* libinput and the Wayland `tablet-v2` protocol already support pressure, tilt, rotation, rings, strips
  and high-resolution dials for Wayland-native applications built on GTK, Qt and SDL.

What we are proposing:

* **libtablet** (`gitlab.freedesktop.org/libtablet`): the continuation of libwacom under a neutral
  freedesktop.org group. Each vendor gets data stewards who can merge their own device descriptions.
* **A written kernel interface** for tablets (`Documentation/input/tablet-guidelines.rst`, under
  review on linux-input). A device that follows it, with standard HID digitizer descriptors, unique USB
  product IDs and stylus serial numbers, needs **no driver code at all**.
* **A portable profile format** (`$XDG_CONFIG_HOME/xdg-tablet/profiles.toml`) so artists' settings
  carry over between desktops.

What we'd ask of you:

1. Submit a `.tablet` description and an SVG button layout for each model before it ships.
2. Submit report descriptors and short `hid-recorder` recordings for each model.
3. Use a unique USB product ID per hardware revision. If that's impossible, expose a model
   identifier in a USB string descriptor and tell us where it is.
4. Nominate one or two engineers as data stewards for your devices.

We'd be glad to set up a call to walk through the details and your current Linux driver's feature set,
so that nothing your users rely on is lost.

Best regards,
`<name>`
