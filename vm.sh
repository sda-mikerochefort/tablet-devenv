#!/bin/bash
# Run a command inside a virtme-ng guest booted from /src/linux (no KVM here,
# so TCG emulation). The guest sees the container filesystem, so /src and
# /opt/tablet are available. /dev/uhid, /dev/uinput and bpffs are present.
# Usage (inside the container): /src/tablet-devenv/vm.sh "<command>"
# KDIR selects the kernel tree to boot (default /src/linux).
cd "${KDIR:-/src/linux}" || exit 1
KVM=$([ -e /dev/kvm ] || echo --disable-kvm)
exec vng $KVM --rwdir=/src/_build --rwdir=/tmp --memory 4G --cpus "$(nproc)" -- \
  "mount -t bpf bpf /sys/fs/bpf 2>/dev/null; export PATH=/opt/tablet/bin:\$PATH LD_LIBRARY_PATH=/opt/tablet/lib64 PYTHONPATH=/src/hid-tools:\$(ls -d /opt/tablet/lib*/python3*/site-packages 2>/dev/null | tr '\n' :); $*"
