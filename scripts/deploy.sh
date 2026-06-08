#!/usr/bin/env bash
set -euo pipefail
#
# Copy the cross-built binary (and an optional server config) to the device and
# print the run sequence. Requires developer mode + SSH access.
# USB connection exposes the device at 10.11.99.1 by default (override RM_DEVICE).
#
# Set the NATS server here so the app connects on launch without typing:
#
#   # local plaintext:
#   RM_SERVER=nats://192.168.178.35:4222 scripts/deploy.sh
#   # Synadia Cloud (NGS) over TLS + creds:
#   RM_SERVER=tls://connect.ngs.global \
#     RM_CREDS=~/.config/nats/ngs-premium.creds scripts/deploy.sh
#
# RM_SERVER / RM_CREDS are written to ~/agents.json on the device (the creds file
# is copied too, chmod 600). The roster then fills in via $SRV discovery.
#
# To enable the in-app NATS context picker on the device (it has no NATS CLI), push
# selected contexts from ~/.config/nats/context/ — creds paths are rewritten + copied:
#   RM_CONTEXTS=ngs-premium scripts/deploy.sh

DEVICE="${RM_DEVICE:-root@10.11.99.1}"
RM_SERVER="${RM_SERVER:-}"
RM_CREDS="${RM_CREDS:-}"
RM_CONTEXTS="${RM_CONTEXTS:-}"
cd "$(dirname "$0")/.."
BIN="build-device/rm-agents"

[[ -f "$BIN" ]] || { echo "Missing $BIN — run scripts/build-device.sh first." >&2; exit 1; }

# Copy to a temp name then atomically rename over the target. A plain scp fails
# with "Text file busy" (ETXTBSY) if the app is currently running on the device;
# rename replaces the directory entry while the running process keeps its inode,
# so the NEXT launch gets the new binary.
scp "$BIN" "$DEVICE:rm-agents.new"
ssh "$DEVICE" 'mv -f rm-agents.new rm-agents'
echo "Copied binary to ${DEVICE}:~/rm-agents (atomic; safe while it's running)"

remote_creds=""
if [[ -n "$RM_CREDS" ]]; then
  [[ -f "$RM_CREDS" ]] || { echo "RM_CREDS not found: $RM_CREDS" >&2; exit 1; }
  base="$(basename "$RM_CREDS")"
  scp "$RM_CREDS" "$DEVICE:$base"
  ssh "$DEVICE" "chmod 600 '$base'"
  remote_creds="/home/root/$base"   # device home for root
  echo "Wrote ${DEVICE}:~/$base (chmod 600) — the device holds creds only"
fi

if [[ -n "$RM_SERVER" || -n "$remote_creds" ]]; then
  fields=()
  [[ -n "$RM_SERVER" ]]     && fields+=("\"server\": \"$RM_SERVER\"")
  [[ -n "$remote_creds" ]]  && fields+=("\"creds\": \"$remote_creds\"")
  json="{ $(IFS=, ; echo "${fields[*]}") }"
  printf '%s\n' "$json" | ssh "$DEVICE" 'cat > agents.json'
  echo "Wrote ${DEVICE}:~/agents.json -> $json"
fi

# In-app NATS context picker: the device has no NATS CLI, so push selected contexts
# from ~/.config/nats/context/. Each context's creds path is rewritten to the device
# location and the creds file copied (chmod 600). Once present, the picker appears
# in the roster (between the server field and Connect) and cycles them.
#   RM_CONTEXTS=ngs-premium,work scripts/deploy.sh
if [[ -n "$RM_CONTEXTS" ]]; then
  ssh "$DEVICE" 'mkdir -p .config/nats/context'
  IFS=',' read -ra _ctx_names <<< "$RM_CONTEXTS"
  for name in "${_ctx_names[@]}"; do
    name="${name//[[:space:]]/}"
    src="$HOME/.config/nats/context/${name}.json"
    [[ -f "$src" ]] || { echo "  context '$name' not found ($src) — skipped" >&2; continue; }
    url="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("url",""))' "$src")"
    creds_src="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("creds",""))' "$src")"
    dev_creds=""
    if [[ -n "$creds_src" && -f "$creds_src" ]]; then
      cbase="$(basename "$creds_src")"
      scp "$creds_src" "$DEVICE:$cbase"; ssh "$DEVICE" "chmod 600 '$cbase'"
      dev_creds="/home/root/$cbase"
    fi
    if [[ -n "$dev_creds" ]]; then
      printf '{ "url": "%s", "creds": "%s" }\n' "$url" "$dev_creds"
    else
      printf '{ "url": "%s" }\n' "$url"
    fi | ssh "$DEVICE" "cat > .config/nats/context/${name}.json"
    echo "Pushed context '$name' (url=$url${dev_creds:+ + creds})"
  done
fi

cat <<EOF

Run it on the device (the app draws to the e-paper panel; stop the stock UI first):

  ssh ${DEVICE}
  systemctl stop xochitl
  QT_QUICK_BACKEND=epaper ./rm-agents -platform epaper
  # ... use it on the panel ...  press Ctrl-C to quit, then restore the stock UI:
  systemctl start xochitl

One-liner that ALWAYS restores xochitl even if the app errors or you Ctrl-C:

  ssh ${DEVICE} 'systemctl stop xochitl; \\
    trap "systemctl start xochitl" EXIT; \\
    QT_QUICK_BACKEND=epaper ./rm-agents -platform epaper'

Notes:
 - Paper Pro (ferrari) does NOT need the touch rotate/invert env vars from the
   rm1/rm2 docs.
 - libqsgepaper.so is already on this device — no copy needed.
EOF
