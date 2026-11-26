# Battery

**See your wireless device batteries on the Windows taskbar.**

Battery shows charge levels for headsets, mice, keyboards and controllers. You can add hardware readings next to them. It is a lean fork of BatteryHub by KrisGFX and its contributors.

![Headset at 31% and mouse at 50% on the taskbar, each with a level bar](docs/taskbar.png)

## Download

You’ll need **Windows 10 or 11**. Check the [supported devices](docs/devices.md) for your exact model and connection type.

1. Open the [latest release](https://github.com/ahizechi/Battery/releases/latest).
2. Download `BatteryHub-Setup-<version>.exe` under **Assets** and run it. It needs no administrator rights. The app and installer use the BatteryHub name.
   The installer is not code-signed, so Windows may show a blue "Windows protected your PC" box: choose **More info**, then **Run anyway**.
3. Look for the battery readings next to the Windows clock. A portable ZIP is also available on the release page.

To verify a download, compare its SHA-256 with the matching entry in the release’s `SHA256SUMS.txt`. The app verifies updates with this fork’s signing key. See [release verification](docs/releasing.md) for details.

## Get started

1. Connect a supported wireless device and turn it on.
2. Right-click Battery to show, hide or reorder devices, set alert thresholds and choose which readings appear.
3. Open **Hardware on the taskbar** to add readings such as CPU, GPU, memory, disk or network activity. Each is off by default.

## What it does

- **Battery levels.** Each visible device has a percentage and a thin level bar that changes colour while charging or when low.
- **Low-battery alerts.** Set the threshold per device.
- **Optional hardware readings.** Battery can show usage, temperature, power and network readings. It pauses hardware polling while the display is off.
- **Direct device readings.** Battery talks to supported devices and Windows APIs. See the [guide](docs/guide.md) for device options, portable use, Linux and building from source.

## Support

For a missing device or an incorrect reading, [open a device report](https://github.com/ahizechi/Battery/issues/new?template=device-report.yml). Include the model, connection type, app version and what you observed. If you share output from `BatteryHub.exe --dump`, review it first and remove personal information.

Use the [issue picker](https://github.com/ahizechi/Battery/issues/new/choose) for other problems with this fork. For security issues, follow [SECURITY.md](SECURITY.md). See [CONTRIBUTING.md](CONTRIBUTING.md) for build and contribution details.

## Privacy and licence

Device and hardware readings are processed on your PC. The automatic update check contacts GitHub and accepts updates signed for this fork. The optional device support window shows what it sends before you submit a report. See the [guide](docs/guide.md) for reporting and updates.

BatteryHub by KrisGFX and its contributors supplied the original app and most of its device support. See [credits](docs/credits.md) for their work, support links, and the other projects behind Battery. Battery is distributed under the [GNU GPL v3 or later](LICENSE).
