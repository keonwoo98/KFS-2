#!/bin/sh
# One-time rootless-podman setup for the 42 cluster (Fedora, no sudo).
# Home quota is tiny, so relocate container image storage to /goinfre.
set -eu

STORE="/goinfre/$USER/containers"
CONF_DIR="$HOME/.config/containers"

mkdir -p "$STORE/storage" "$CONF_DIR"

cat > "$CONF_DIR/storage.conf" <<EOF
[storage]
driver = "overlay"
graphroot = "$STORE/storage"
EOF

echo "podman image storage relocated to: $STORE/storage"
echo "note: if podman was used before on this account, run: podman system reset"
echo "then build & test with:  make CONTAINER=podman test"
