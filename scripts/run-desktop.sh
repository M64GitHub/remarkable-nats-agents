#!/usr/bin/env bash
set -euo pipefail
#
# Fast UI iteration on the desktop (Tux64 / any Qt6 host) — no device needed.
# Loads the QML directly with the Qt6 'qml' runtime.
#
#   Ubuntu 24.04:  sudo apt install qml6-module-qtquick \
#                       qml6-module-qtquick-window qt6-declarative-dev
#   (macOS:        brew install qt)
#
cd "$(dirname "$0")/.."

QML_BIN="$(command -v qml || command -v qml6 || true)"
if [[ -z "$QML_BIN" ]]; then
  echo "No 'qml'/'qml6' runtime found. Install Qt6 (see comments in this script)." >&2
  exit 1
fi
exec "$QML_BIN" src/Main.qml
