#!/usr/bin/env bash
set -euo pipefail
#
# Read-only capability probe of the connected reMarkable Paper Pro.
# Requires developer mode + SSH access. Makes NO changes to the device.
# Override the target with RM_DEVICE if it isn't root@10.11.99.1.
#
#   RM_DEVICE=root@10.11.99.1 scripts/inspect-device.sh

DEVICE="${RM_DEVICE:-root@10.11.99.1}"

echo "== Probing $DEVICE (read-only) =="
ssh "$DEVICE" 'bash -s' <<'REMOTE'
set +e

echo "### OS / arch"
grep -E '^(NAME|VERSION)=' /etc/os-release 2>/dev/null
uname -a

echo; echo "### CPU / memory"
grep -m1 -E 'model name|CPU part|Features' /proc/cpuinfo 2>/dev/null
echo "nproc: $(nproc 2>/dev/null)"
free -h 2>/dev/null || head -3 /proc/meminfo

echo; echo "### Qt core + qml roots"
find /usr -maxdepth 4 -name 'libQt6Core.so*' 2>/dev/null
find /usr -maxdepth 4 -type d -name qml 2>/dev/null

echo; echo "### Available QML modules"
for d in $(find /usr -maxdepth 4 -type d -name qml 2>/dev/null); do
  echo "-- under $d:"
  ls "$d" 2>/dev/null
  [ -d "$d/QtQuick" ] && ls "$d/QtQuick" 2>/dev/null | sed 's/^/   QtQuick\//'
done

echo; echo "### Platform plugins (looking for epaper)"
find /usr -path '*plugins/platforms*' -name '*.so' 2>/dev/null

echo; echo "### Scenegraph plugins (looking for libqsgepaper.so)"
find /usr -path '*scenegraph*' -name '*.so' 2>/dev/null

echo; echo "### Input contexts / virtual keyboard"
find /usr -path '*platforminputcontexts*' -name '*.so' 2>/dev/null
find /usr -type d -name 'VirtualKeyboard' 2>/dev/null

echo; echo "### Input devices (touch / keys)"
grep -E '^N:|^H:' /proc/bus/input/devices 2>/dev/null
ls -1 /dev/input/ 2>/dev/null

echo; echo "### Fonts (first 20)"
{ fc-list 2>/dev/null | head -20; } || find /usr/share/fonts -name '*.ttf' 2>/dev/null | head -20

echo; echo "### xochitl"
echo "active: $(systemctl is-active xochitl 2>/dev/null)"

echo; echo "(probe complete)"
REMOTE
echo
echo "== Done. Use these findings for module choices, libqsgepaper.so, and the keyboard approach. =="
