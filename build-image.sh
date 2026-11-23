#!/bin/bash
# Build the tablet-dev:f45 image. In Claude Code cloud sessions outbound TLS is
# re-terminated by an agent proxy, so its CA is baked in when present;
# elsewhere an empty placeholder is used and nothing extra is trusted.
set -e
cd "$(dirname "$0")"
docker info >/dev/null 2>&1 || { (nohup dockerd >/var/log/dockerd.log 2>&1 &); sleep 5; }
if [ -s /root/.ccr/ca-bundle.crt ]; then cp /root/.ccr/ca-bundle.crt ca-bundle.crt; else : > ca-bundle.crt; fi
exec docker build -f Containerfile -t tablet-dev:f45 "$@" .
