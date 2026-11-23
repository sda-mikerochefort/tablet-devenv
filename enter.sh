#!/bin/bash
# Enter (or run a command in) the Fedora 45 tablet dev container.
# Persistent /opt/tablet install prefix lives in a named volume.
docker info >/dev/null 2>&1 || { (nohup dockerd >/var/log/dockerd.log 2>&1 &); sleep 5; }
exec docker run --rm -i $([ -t 0 ] && echo -t) --privileged \
  -v /home/user:/src -v tablet-prefix:/opt/tablet -v tablet-cargo:/root/.cargo \
  -v tablet-ccache:/root/.cache/ccache \
  -w /src tablet-dev:f45 "${@:-bash}"
