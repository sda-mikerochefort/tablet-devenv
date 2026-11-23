#!/bin/bash
# Build the userspace stack in dependency order into /opt/tablet.
# Run inside the tablet-dev:f45 container with /home/user mounted at /src.
set -uo pipefail
PREFIX=${PREFIX:-/opt/tablet}
B=/src/_build
mkdir -p $B
declare -A RC
step() { local name=$1; shift; echo "=== $name"; if "$@" > $B/$name.log 2>&1; then RC[$name]=ok; else RC[$name]="FAIL (see _build/$name.log)"; fi; }

ms() { # meson setup+compile+install
  local src=$1; shift
  meson setup --wipe $B/$(basename $src) $src --prefix=$PREFIX --libdir=lib64 "$@" &&
  meson compile -C $B/$(basename $src) && meson install -C $B/$(basename $src)
}

step wayland-protocols ms /src/wayland-protocols -Dtests=true
step libtablet         ms /src/libtablet -Dtests=enabled -Ddocumentation=disabled
step libinput          ms /src/libinput -Ddocumentation=false -Ddebug-gui=true -Dtests=true -Dlibwacom=true
step hid-tools         pip install --break-system-packages --prefix=$PREFIX -e /src/hid-tools
step udev-hid-bpf      ms /src/udev-hid-bpf --native-file /etc/meson/bpf-clang21.ini
step mutter            ms /src/mutter -Dtests=enabled -Dprofiler=false -Dbash_completion=false

echo; for k in wayland-protocols libtablet libinput hid-tools udev-hid-bpf mutter; do printf '%-18s %s\n' $k "${RC[$k]}"; done
