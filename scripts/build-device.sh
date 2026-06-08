#!/usr/bin/env bash
set -euo pipefail
#
# Cross-compile for reMarkable Paper Pro (code name: ferrari, target: aarch64).
#
# IMPORTANT: run this on an x86_64 LINUX host. The official reMarkable SDK only
# ships x86_64-host toolchains, so it does NOT run on an Apple-silicon Mac.
# (Your Tuxedo / Ubuntu box is the right machine.)
#
# 1. Download the Paper Pro (ferrari) SDK from:
#       https://developer.remarkable.com/links
#    Match it to your device's OS version (Settings -> Software, or on-device:
#       cat /etc/os-release | grep ^VERSION=). A near version is fine (we cross-
#       check the sysroot Qt against the device).
# 2. Install it, e.g.:
#       chmod u+x meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh
#       ./meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk
# 3. Each shell that builds for the device must source the env file. This script
#    does it for you; override the location with RM_SDK_ENV if it differs.

# Honor RM_SDK_ENV if set; otherwise try the common install layouts.
if [[ -z "${RM_SDK_ENV:-}" ]]; then
  for cand in \
    "$HOME/rm-sdk/environment-setup-cortexa53-crypto-remarkable-linux" \
    "$HOME/rm-sdk/ferrari/environment-setup-cortexa53-crypto-remarkable-linux"; do
    [[ -f "$cand" ]] && { RM_SDK_ENV="$cand"; break; }
  done
fi

if [[ -z "${RM_SDK_ENV:-}" || ! -f "$RM_SDK_ENV" ]]; then
  echo "SDK env file not found (looked under ~/rm-sdk)." >&2
  echo "Install the ferrari SDK and/or set RM_SDK_ENV to its environment-setup-* file." >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$RM_SDK_ENV"
echo "Cross-compiler: ${CC%% *}"

cd "$(dirname "$0")/.."
# The SDK env exports CMAKE_TOOLCHAIN_FILE, so cmake cross-compiles automatically.
cmake -S . -B build-device -DCMAKE_BUILD_TYPE=Release
cmake --build build-device --parallel

echo
echo "Built: build-device/hello_remarkable"
echo "Deploy it with: scripts/deploy.sh"
