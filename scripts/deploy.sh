#!/usr/bin/env bash
set -euo pipefail
#
# Copy the cross-built binary (and an optional server config) to the device and
# print the run sequence. Requires developer mode + SSH access.
# USB connection exposes the device at 10.11.99.1 by default (override RM_DEVICE).
#
# Because the device has no on-screen keyboard yet, set the NATS server here so
# the app connects on launch without typing:
#
#   RM_SERVER=nats://192.168.178.35:4222 scripts/deploy.sh
#
# That writes ~/agents.json on the device; the app reads it on startup and the
# roster then fills in via $SRV discovery.

DEVICE="${RM_DEVICE:-root@10.11.99.1}"
RM_SERVER="${RM_SERVER:-}"
cd "$(dirname "$0")/.."
BIN="build-device/hello_remarkable"

[[ -f "$BIN" ]] || { echo "Missing $BIN — run scripts/build-device.sh first." >&2; exit 1; }

scp "$BIN" "$DEVICE:"
echo "Copied binary to ${DEVICE}:~/hello_remarkable"

if [[ -n "$RM_SERVER" ]]; then
  tmp="$(mktemp)"
  printf '{ "server": "%s" }\n' "$RM_SERVER" > "$tmp"
  scp "$tmp" "$DEVICE:agents.json"
  rm -f "$tmp"
  echo "Wrote ${DEVICE}:~/agents.json  (server: $RM_SERVER)"
fi

cat <<EOF

Run it on the device (the app draws to the e-paper panel; stop the stock UI first):

  ssh ${DEVICE}
  systemctl stop xochitl
  QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper
  # ... use it on the panel ...  press Ctrl-C to quit, then restore the stock UI:
  systemctl start xochitl

One-liner that ALWAYS restores xochitl even if the app errors or you Ctrl-C:

  ssh ${DEVICE} 'systemctl stop xochitl; \\
    trap "systemctl start xochitl" EXIT; \\
    QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper'

Notes:
 - Paper Pro (ferrari) does NOT need the touch rotate/invert env vars from the
   rm1/rm2 docs.
 - libqsgepaper.so is already on this device — no copy needed.
EOF
