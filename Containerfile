# Fedora 45 development image for the unified tablet stack
# (linux HID/HID-BPF, udev-hid-bpf, hid-tools, libtablet/libwacom, libinput,
#  wayland-protocols, mutter).
FROM registry.fedoraproject.org/fedora:45

# Proxy CA for sandboxed sessions (empty placeholder elsewhere); see build-image.sh.
COPY ca-bundle.crt /etc/pki/ca-trust/source/anchors/ccr-agent-proxy.crt
RUN update-ca-trust

RUN dnf -y install dnf5-plugins && dnf -y upgrade

# Core toolchain + kernel build / HID-BPF / selftests
RUN dnf -y install \
      git git-email b4 patch diffutils which make gcc gcc-c++ clang llvm lld \
      bison flex bc perl rsync cpio kmod openssl openssl-devel \
      elfutils-libelf-devel elfutils-devel dwarves ncurses-devel \
      libcap-devel zlib-devel zstd xz pahole \
      bpftool libbpf-devel libbpf-tools \
      qemu-system-x86-core virtme-ng busybox \
      sparse coccinelle codespell python3-ply python3-GitPython \
      ccache gdb strace valgrind

# Meson / Rust / Python tooling (udev-hid-bpf, hid-tools, libinput, libwacom)
RUN dnf -y install \
      meson ninja-build cmake pkgconf-pkg-config \
      rust cargo rustfmt clippy \
      python3 python3-pip python3-devel python3-pytest python3-pytest-xdist \
      python3-libevdev python3-pyudev python3-parse python3-click \
      python3-yaml python3-typing-extensions python3-hatchling \
      python3-gobject python3-dbus python3-black python3-ruff python3-rich python3-pyroute2 python3-jsonschema \
      systemd-devel libudev-devel libevdev-devel libevdev-utils mtdev-devel \
      check-devel glib2-devel libgudev-devel lua-devel \
      wayland-devel wayland-protocols-devel libxml2 \
      umockdev umockdev-devel evemu evtest hidapi-devel \
      librsvg2-tools inkscape-docs doxygen graphviz python3-sphinx \
      python3-recommonmark python3-sphinx_rtd_theme

# Pull distro build dependencies for each project (skips anything missing)
RUN dnf -y builddep --skip-unavailable \
      libinput libwacom wayland-protocols mutter udev-hid-bpf \
 && dnf clean all

# Runtime/dev helpers for compositor testing (headless mutter, libei, dbus)
RUN dnf -y install --skip-unavailable \
      libei-devel libeis-devel dbus-daemon xorg-x11-server-Xvfb \
      gnome-desktop4-devel gsettings-desktop-schemas-devel \
      mesa-dri-drivers mesa-libEGL-devel weston \
 && dnf clean all

# udev-hid-bpf upstream CI pins Fedora 43 (clang 21). Fedora 45's clang 23
# emits libc memcpy calls / oversized stack temporaries for several BPF
# programs, so build BPF objects with the clang21 compat package.
RUN dnf -y install clang21 llvm21 && dnf clean all
COPY bpf-clang21.ini /etc/meson/bpf-clang21.ini

# Find /opt/tablet libraries through ld.so.conf rather than LD_LIBRARY_PATH:
# LD_LIBRARY_PATH would override the RUNPATH of uninstalled test binaries,
# so test suites would silently run against the installed copy instead of
# the build tree. enter.sh runs ldconfig on start (the prefix is a volume).
RUN echo /opt/tablet/lib64 > /etc/ld.so.conf.d/opt-tablet.conf

ENV PATH=/root/.cargo/bin:/usr/local/bin:$PATH \
    PKG_CONFIG_PATH=/opt/tablet/lib64/pkgconfig:/opt/tablet/share/pkgconfig \
    XDG_DATA_DIRS=/opt/tablet/share:/usr/share
WORKDIR /src
