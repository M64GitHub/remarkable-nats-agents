#!/usr/bin/env bash
set -euo pipefail
#
# Fast UI iteration on the desktop (Tux64 / any Qt6 host) — no device needed.
#
# The app now registers C++ types (the NATS client, the models) and bundles its
# QML as a module, so we build the real binary and run it — the bare `qml`
# runtime can't load the C++ side. This stays a logic/layout preview, never an
# e-paper fidelity check.
#
# Qt: we use the pinned Qt 6.8 under ~/Qt (installed via aqtinstall) so the
# preview matches the device's Qt 6.8.2. Override with QT_ROOT if yours differs.
#   QT_ROOT=~/Qt/6.8.2/gcc_64 scripts/run-desktop.sh
#
cd "$(dirname "$0")/.."

QT_ROOT="${QT_ROOT:-$HOME/Qt/6.8.2/gcc_64}"
if [[ ! -x "$QT_ROOT/bin/qmake" && ! -d "$QT_ROOT/lib/cmake/Qt6" ]]; then
  echo "Qt6 not found at QT_ROOT=$QT_ROOT" >&2
  echo "Install it (matches device 6.8.2):" >&2
  echo "  python3 -m venv ~/.venvs/aqt && ~/.venvs/aqt/bin/pip install aqtinstall" >&2
  echo "  ~/.venvs/aqt/bin/aqt install-qt linux desktop 6.8.2 linux_gcc_64 --outputdir ~/Qt" >&2
  exit 1
fi

cmake -S . -B build-desktop -DCMAKE_PREFIX_PATH="$QT_ROOT" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-desktop --parallel

# Use the desktop Qt's libraries at runtime.
export LD_LIBRARY_PATH="$QT_ROOT/lib:${LD_LIBRARY_PATH:-}"
exec ./build-desktop/hello_remarkable "$@"
