# hello-remarkable

A minimal Qt Quick demo for the **reMarkable Paper Pro** (`ferrari`, aarch64).
Develop the UI offline on your desktop; cross-compile and deploy when ready.

## 0. One-time: enable Developer Mode on the device
Required before you can SSH in or run custom binaries:
https://developer.remarkable.com/documentation/developer-mode

## 1. Iterate the UI on your desktop (no device, no SDK)
reMarkable apps are pure QML, so the UI runs in the desktop `qml` runtime.

```sh
# macOS
brew install qt
# Ubuntu
sudo apt install qml6-module-qtquick qt6-declarative-dev

scripts/run-desktop.sh        # or: qml src/Main.qml
```

Touch on-device arrives as mouse events, so `MouseArea` behaves the same here.

## 2. Cross-compile for the device (x86_64 Linux host)
The reMarkable SDK ships **only** as an x86_64-Linux-host toolchain — it does not
run on Apple silicon. Build on your x86_64 Linux machine.

```sh
# Download the ferrari SDK matching your device's OS version:
#   https://developer.remarkable.com/links
# (Check version on device:  cat /etc/os-release | grep ^VERSION= )

chmod u+x meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh
./meta-toolchain-remarkable-<ver>-ferrari-public-x86_64-toolchain.sh -d ~/rm-sdk/ferrari

scripts/build-device.sh       # sources the SDK env and builds build-device/hello_remarkable
```

If your SDK env file lives elsewhere: `RM_SDK_ENV=/path/to/environment-setup-* scripts/build-device.sh`

## 3. Deploy and run
Connect the device over USB (defaults to `10.11.99.1`):

```sh
scripts/deploy.sh             # scp + prints the run sequence
```

Then on the device:

```sh
ssh root@10.11.99.1
systemctl stop xochitl
QT_QUICK_BACKEND=epaper ./hello_remarkable -platform epaper
# Ctrl-C when done
systemctl start xochitl
```

## Notes
- Paper Pro does **not** need the touch rotate/invert env vars the rm1/rm2 docs mention.
- On software **3.17**, you may need to copy `libqsgepaper.so` from the SDK sysroot
  to `/usr/lib/plugins/scenegraph/` on the device (see `scripts/deploy.sh`).
- Pure Qt Quick only — **no Qt Widgets**.
- Marker/pen input is more involved and intentionally omitted from this starter.

## References
- SDK: https://developer.remarkable.com/documentation/sdk
- Qt Quick on e-paper: https://developer.remarkable.com/documentation/qt_epaper
- Official examples: https://github.com/reMarkable/remarkable-developer-examples
