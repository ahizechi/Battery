# Battery

**Your wireless batteries, on the Windows taskbar.**

Battery shows the charge of your headsets, mice, keyboards, controllers, earbuds and phone next to the clock, with a
level bar under each one. It is a lean fork of BatteryHub by KrisGFX and its contributors.

<img src="docs/taskbar.png" width="462" alt="A headset at 72%, a mouse charging at 46% with a green bar, and a full keyboard on the Windows taskbar">

## Install

Battery runs on Windows 10 and 11. Check the [supported devices](docs/devices.md) for your model and how it connects.

1. Download `BatteryHub-Setup-<version>.exe` from the [latest release](https://github.com/ahizechi/Battery/releases/latest)
   and run it. It needs no administrator rights. The app keeps the BatteryHub name.
2. The installer is not code-signed, so Windows may say **Windows protected your PC**. Choose **More info**, then
   **Run anyway**.
3. Your devices appear next to the clock.

Prefer not to install? Each release also has a portable ZIP.

Battery checks for updates once a day and installs only updates signed for this fork. To check a download yourself,
compare its SHA-256 with the release's `SHA256SUMS.txt` ([how releases are signed](docs/releasing.md)).

## Use

Right-click the readings on the taskbar:

- **Level bars** fill with the charge, turn green while a device charges and red when it runs low.
- **Battery alerts** warn you before a device dies. Set the threshold per device (15% by default).
- **Hardware on the taskbar** adds CPU, GPU, memory, disk, network and SSD readings. Each stays off, and is not read
  at all, until you switch it on.
- **Show as tray icons** gives each device its own icon instead of the taskbar strip.
- Rename, reorder or hide devices, and choose which sources Battery reads.

The [guide](docs/guide.md) covers every option, the command line, Linux and building from source.

## Help

Device missing or reading wrong? [Open a device report](https://github.com/ahizechi/Battery/issues/new?template=device-report.yml)
with the model, how it connects, the app version and what you saw. If you attach `BatteryHub.exe --dump` output,
read it first and remove anything personal.

For anything else, use the [issue picker](https://github.com/ahizechi/Battery/issues/new/choose). Security issues go
through [SECURITY.md](SECURITY.md), and [CONTRIBUTING.md](CONTRIBUTING.md) explains how to build and contribute.

## Privacy

Battery reads and processes everything on your PC. It goes online only to check GitHub for updates (you can turn
this off) and when you send **Request support for a device**, which shows exactly what it will send first.

## Credits and licence

BatteryHub by KrisGFX and its contributors built the original app and most of its device support. You can support
KrisGFX on [Ko-fi](https://ko-fi.com/krisgfx) or [PayPal](https://www.paypal.com/paypalme/krisgfx).
[Credits](docs/credits.md) lists the other projects behind Battery.

Battery is free software under the [GNU GPL v3 or later](LICENSE).
