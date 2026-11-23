# Unified tablet stack — Fedora 45 dev environment

The session container is Ubuntu 24.04, so all builds run inside a Fedora 45
container image (`tablet-dev:f45`) built from `Containerfile` in this directory.

## Layout

| Path (host)              | In container  | Upstream (remote `upstream`)                          | Dev branch base        |
|--------------------------|---------------|-------------------------------------------------------|------------------------|
| `/home/user/linux`       | `/src/linux`  | git.kernel.org torvalds/linux `master` (+ remote `hid` = hid/hid.git `for-next`) | v7.4-rc4 |
| `/home/user/udev-hid-bpf`| `/src/...`    | gitlab.freedesktop.org/libevdev/udev-hid-bpf `main`   | 2.3.0-20260703+        |
| `/home/user/hid-tools`   |               | gitlab.freedesktop.org/libevdev/hid-tools `master`    | 0.12+                  |
| `/home/user/libtablet`   |               | github.com/linuxwacom/libwacom `master`               | libwacom-2.20.0+       |
| `/home/user/libinput`    |               | gitlab.freedesktop.org/libinput/libinput `main`       | 1.32.0+                |
| `/home/user/wayland-protocols` |         | gitlab.freedesktop.org/wayland/wayland-protocols `main` | 1.50                 |
| `/home/user/mutter`      |               | gitlab.gnome.org/GNOME/mutter `main`                  | 51.1+                  |
| `/home/user/tablet-hid-descriptors` |   | github.com/linuxwacom/wacom-hid-descriptors `master` | 3dd4a3d (378 commits) |

Every repo has the branch `claude/elegant-goldberg-5odfs6` checked out. That
branch is pushed to `origin` (github.com/sda-mikerochefort/<repo>).

## Usage

```sh
./tablet-devenv/build-image.sh                    # once per session (starts dockerd if needed)
./tablet-devenv/enter.sh                          # interactive shell, /home/user -> /src
./tablet-devenv/enter.sh ./tablet-devenv/build-userspace.sh   # build all userspace into /opt/tablet
```

* Install prefix: `/opt/tablet`, kept in the docker volume `tablet-prefix`. `PKG_CONFIG_PATH`, `LD_LIBRARY_PATH` and `XDG_DATA_DIRS` point at it, so libinput builds against our libtablet and mutter builds against our libinput and wayland-protocols.
* Build dirs are under `/home/user/_build/<project>`, and logs are in `_build/*.log`.
* udev-hid-bpf **must** be configured with `--native-file /etc/meson/bpf-clang21.ini`. Fedora 45's clang 23 breaks 5 BPF programs: Gaomon M6, MS Surface Dial, XP-Pen Artist12 3rd, Deco Pro V1, and Deco SE. It emits libc `memcpy` calls and blows the 512-byte BPF stack. Upstream CI pins Fedora 43 (clang 21). The same applies to the kernel's `drivers/hid/bpf/progs` (for example Huion Kamvas Pro 19), so build those with `CLANG=clang-21`. A partial source fix for Deco SE is in `udev-hid-bpf-decose-clang23.patch`. That is worth upstreaming together with fixes for the other four.
* Kernel (inside the container):
  ```sh
  cd /src/linux
  vng --build --config tools/testing/selftests/hid/config        # first time only
  scripts/kconfig/merge_config.sh -m .config /src/tablet-devenv/kernel-tablet.config && make olddefconfig
  make -j$(nproc) CC="ccache gcc" bzImage
  make -C tools/testing/selftests/hid
  make -C drivers/hid/bpf/progs CLANG=clang-21     # needed by tests/test_tablet.py (clang 23 emits memcpy)
  /src/tablet-devenv/vm.sh "cd tools/testing/selftests/hid && ./hid_bpf"
  ```
  `vng --build` ignores a second `--config` and keeps an existing `.config`, so merge
  `kernel-tablet.config` by hand. It adds uinput, hid-uclogic, hid KUnit, LED multicolor,
  hidpp and battery options.
* `vm.sh "<cmd>"` boots that kernel under virtme-ng (TCG, about 8 s to a shell). It mounts bpffs,
  which the `hid_bpf` attach/detach tests need, and puts `/opt/tablet` on `PATH`. Run all device-level
  tests through it.

## USB device emulation (usb-emu)

`usb-emu/` emulates USB HID tablets with raw-gadget + dummy_hcd inside the VM. That reaches USB-only
paths uhid can't: hid-uclogic's string-descriptor probing and HID-BPF string-descriptor reads.
It needs `CONFIG_USB_DUMMY_HCD`/`CONFIG_USB_RAW_GADGET` (in `kernel-tablet.config`). `vm.sh` boots the
kernel tree in `$KDIR` (default `/src/linux`):

```sh
./tablet-devenv/enter.sh /src/tablet-devenv/vm.sh \
  "/src/tablet-devenv/usb-emu/run-test.sh /src/tablet-devenv/usb-emu/devices/huion-hs610.desc \
                                          /src/tablet-devenv/usb-emu/devices/hs610-demo.script"
```

See `usb-emu/README.md`.

## Known limitations of this sandbox

* The container itself has no `/dev/kvm`, `/dev/uhid` or `/dev/uinput`, and no loadable modules. Run device tests (libinput, hid-tools, udev-hid-bpf live tests, kernel selftests) through `vm.sh`. Without KVM, that guest runs under TCG emulation.
* The `docker` daemon is not started automatically. `enter.sh` starts it if needed.

## Baseline test status (upstream HEAD, unmodified)

| Project            | Build | Tests |
|--------------------|-------|-------|
| wayland-protocols  | ok    | 197/197 pass |
| libtablet (libwacom) | ok  | 17/17 pass |
| libinput           | ok    | container: 11 pass, 28 skip, 1 fail (needs uinput). In VM, `--filter-test='*tablet*'`: 0 fail |
| hid-tools          | ok (pip -e) | 465 pass, 3 error in container (need /dev/uhid). Device tests live in the kernel tree now |
| udev-hid-bpf       | ok (clang21) | in VM: 65 pass, 55 skipped (libtest for `stable/` programs not built by default), 2 fail (`test_helpers::TestIteratorMacros`: "Expected 16 input reports, got 0"), still to investigate |
| mutter             | ok    | not run (needs a headless session and dbus) |
| linux v7.4-rc4     | ok    | HID KUnit: hid_input, uclogic core/params/rdesc all ok. selftests `hid_bpf`: 45 pass, 1 skip, 0 fail. `tests/test_{tablet,wacom_generic,multitouch}.py`: 1676 pass, 1065 skip, 5 xfail, 0 fail (in VM, progs built with clang-21) |

## Phase 0 deliverables

See [`phase0/README.md`](phase0/README.md).
