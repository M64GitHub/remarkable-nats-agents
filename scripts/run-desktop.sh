#!/usr/bin/env bash
set -euo pipefail
#
# Fast UI iteration on your desktop (Mac or Linux), no device needed.
# Uses the system Qt6 'qml' runtime to load the UI directly.
#
#   macOS:   brew install qt
#   Ubuntu:  sudo apt install qml6-module-qtquick qt6-declarative-dev
#
# If `qml` is not on your PATH (common on Homebrew), use the full path, e.g.
#   exec "$(brew --prefix qt)/bin/qml" src/Main.qml

cd "$(dirname "$0")/.."
exec qml src/Main.qml
