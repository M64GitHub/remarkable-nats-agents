#!/usr/bin/env bash
set -euo pipefail
#
# Copy the cross-built binary to the device and print the run sequence.
# Requires developer mode enabled on the Paper Pro and SSH access.
# USB connection exposes the device at 10.11.99.1 by default.

DEVICE="${RM_DEVICE:-root@10.11.99.1}"
cd "$(dirname "$0")/.."
BIN="build-device/hello_remarkable"

[[ -f "$BIN" ]] || { echo "Missing $BIN — run scripts/build-device.sh first." >&2; exit 1; }

scp "$BIN" "$DEVICE:"

cat <<EOF

Copied to ${DEVICE}:~/hello_remarkable

Now run it on the device:

  ssh ${DEVICE}
  systemctl stop xochitl
  QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper
  # ... test it ...  Ctrl-C when done, then bring the UI back:
  systemctl start xochitl

Notes:
 - Paper Pro (ferrari) does NOT need the touch rotate/invert env vars that the
   rm1/rm2 docs mention.
 - If your device runs software 3.17 and the app fails to find the epaper
   scenegraph plugin, copy libqsgepaper.so from the SDK sysroot first:
     <sdk>/sysroots/cortexa53-crypto-remarkable-linux/usr/lib/plugins/scenegraph/libqsgepaper.so
   -> /usr/lib/plugins/scenegraph/ on the device.
EOF
