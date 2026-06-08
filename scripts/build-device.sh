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
#       cat /etc/os-release | grep ^VERSION=).
# 2. Install it, e.g.:
#       chmod u+x meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh
#       ./meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk/ferrari
# 3. Point RM_SDK_ENV at the env-setup file (or rely on the default below).

RM_SDK_ENV="${RM_SDK_ENV:-$HOME/rm-sdk/ferrari/environment-setup-cortexa53-crypto-remarkable-linux}"

if [[ ! -f "$RM_SDK_ENV" ]]; then
  echo "SDK env file not found: $RM_SDK_ENV" >&2
  echo "Install the ferrari SDK and/or set RM_SDK_ENV to its environment-setup-* file." >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$RM_SDK_ENV"
echo "Cross-compiler: ${CC%% *}"

cd "$(dirname "$0")/.."
cmake -S . -B build-device
cmake --build build-device

echo
echo "Built: build-device/hello_remarkable"
echo "Deploy it with: scripts/deploy.sh"
