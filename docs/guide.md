# Battery guide

Everything beyond the [README](../README.md): display options, alerts, installing, updates, the command line, building, Linux, limitations and translations.

## Display

**On the taskbar (default):** `🎧 30%  🖱 93%` next to the notification area, drawn with the Windows icon font.
- **Level bar:** a thin bar with round ends sits under each battery percentage, filled to the level, in the taskbar's text colour. Every bar is as wide as "100%", so levels compare at a glance and the strip keeps its width as they change. It turns green while the device charges and when it is full on power, and red when the battery is low (15% or less); the number itself stays in the text colour (this fork's replacement for BatteryHub's ⚡ and ✓). *Show charging* in the menu turns the green, and the tray icon's blue charging bar, off; the hover tooltip still says "charging".
- **Asleep:** dimmed text means the device is asleep and the last known value is shown.
- **Disconnected (optional):** turn on *Show disconnected devices* to display a dimmed `×` for devices whose driver explicitly reports an offline wireless link. This does not infer disconnection from a missed battery poll; the default remains to hide offline devices.
- **Two devices of one kind:** with two mice (or two controllers...) both would get the same icon, so each one gets a tiny caption of up to 3 letters in the corner of its icon, taken from its name (`MX`, `PRO`), or a number when the names do not tell them apart. A device that is alone in its kind shows no caption.
- **Theme:** it follows light/dark mode and DPI scaling.
- **Transparent taskbar themes** (e.g. Windhawk's Taskbar Styler): turn on *Background behind taskbar text* in the menu to keep the text readable over the wallpaper.
- **Several monitors:** the widget shows on every monitor's taskbar, next to its clock, so it stays visible while a game runs fullscreen. *Show the widget on* in the menu picks *All monitors*, *Main monitor only*, or one display (e.g. only *Display 3*).
- **More display options:** *Icons above percentages* stacks each icon over its level to save width; *Brand colors* shows known brands in their color (Razer green, HyperX red, ...), level bar included; two devices of the same kind get a tiny caption so they can be told apart; left and right taskbars get a column of devices.
- **Languages:** English, العربية, Deutsch, Español, Français, עברית, 日本語, 한국어, Polski, Português (Brasil), Русский, Türkçe, Українська, 简体中文 and 繁體中文. The app follows the Windows display language; *Language* in the menu picks another one. See [Translations](#translations) to add or improve a language.
- **Brand colors:** devices of 15 known brands get their maker's color, with a soft glow and a thin level bar under the
  percentage: Razer green, Logitech blue, SteelSeries orange, Corsair yellow, HyperX / ROG / ZOWIE red, PlayStation,
  Xbox, Samsung, Apple, Jabra, JBL, Cooler Master and Steam. Each color has a dark- and a light-taskbar shade. The brand
  comes from the driver that found the device, then the vendor ID, then the name; colors and rules are in
  `src/brands.cpp`. Turn it off with *Brand colors* in the menu.
- **Hardware readings (this fork, off by default):** after the batteries, for example
  `CPU 12%  CPU 51W  GPU 4%  GPU 44°  GPU PWR 13%  VRAM 6%  RAM 61%  DISK 2%  SSD 38°  NET↓ 1.2M  NET↑ 80K`.
  *Hardware on the taskbar* in the menu switches each one on; *Refresh every* picks 1, 2 or 5 seconds (5 by
  default, the cheapest). Nothing is sampled while the display is off. The tooltip lists them by name. A temperature turns orange, then red, from 80 / 90 °C for the GPU and 65 / 75 °C
  for the SSD.
  - *CPU power* is the processor package's draw in watts, from its own energy meter (Windows' Energy Metering
    Interface; recent Intel and AMD processors).
  - *GPU power* is the graphics card's draw as a share of its power limit, as its driver reports it (13% of a
    250 W limit is about 32 W). Watts would need NVIDIA's `nvml.dll`, which adds about 20 MB and never unloads.
  - *SSD temperature* is the Windows drive's own sensor, read every 10 seconds whatever *Refresh every* says:
    the query briefly holds up the app, and a drive's temperature changes slowly.
  - *Disk activity* is the busiest disk's active time, like Task Manager's.
  - *Network download / upload speed* add up the real network adapters (Ethernet, Wi-Fi), in decimal bytes
    per second: `K` = kB/s, `M` = MB/s, `G` = GB/s.

  Readings come from Windows performance counters (GPU and VRAM usage), DXGI, the graphics driver (D3DKMT), the
  disks' and energy meters' own drivers, the network interface table and the storage temperature query on the
  drive that holds Windows: no driver, no admin rights, nothing downloaded. A reading the PC does not provide is
  left out: *GPU temperature* needs a graphics driver that reports it (NVIDIA's `nvml.dll` is not used, as it
  adds about 20 MB). There is no CPU temperature, because
  Windows only exposes it through a kernel driver. Taskbar widget only; nothing is read in tray-icon mode. Idea
  from [TraySense](https://github.com/DMTWEAKS/TraySense); no code was taken from it.
- **Off means not read (this fork):** everything you can switch off stops being read, not just hidden:
  - *Hardware on the taskbar*: each reading's source closes when it is off; GPU and VRAM usage (the costliest,
    they load `pdh.dll`) unload it when both are off.
  - *Read batteries from*: *Bluetooth devices and earbuds* (Windows' Bluetooth levels, AirPods / Fast Pair /
    Galaxy Buds / Nothing Ear, GameSir over Bluetooth, and every Bluetooth HID device), *Phone* (Microsoft Phone
    Link), *Xbox controllers*, and *HeadsetControl* (more headsets). HeadsetControl is **off by default** in
    this fork, because it runs `headsetcontrol.exe` about once a minute; switch it on if a headset only shows
    up through it. The Bluetooth broadcast listener also stops by itself as soon as no AirPods or Bluetooth
    headset needs it.
  - A device switched off with *Show on the taskbar* is no longer opened at all; the menu lists it as *not read
    while hidden*. Exception: when it shares a receiver with a device that is still shown (two devices on one
    Logitech receiver), the receiver is still read.
- **No battery history (this fork):** the original's history window, recording and time-to-empty estimate
  were removed to keep the app small. Nothing is recorded to disk.

**Tray icons:** one icon per device with the number drawn in it, plus a "Low battery" notification at 15%. The app switches to this automatically when the taskbar is vertical or can't be found.

**Custom device names:** open the device submenu and select *Rename device...* to set a name
(up to 64 characters). The name is shown in the taskbar widget's tooltip/caption,
tray tooltip and notifications, and the device menu. Use *Reset to original name*
to restore the driver's name. Names are stored locally in the current user's
Windows registry (`Software\WirelessBattery`, `DeviceAliases`), using the stable
device key. Renaming never changes driver matching, protocol queries, or `--list` /
`--dump` diagnostic output. No account or network access is needed.

Hover for details. Click for the menu: every device (click one to hide or show it), display mode, *Refresh now*, *Start with Windows*, and *Exit*. Levels refresh every 60 seconds (*Check battery every* in the menu changes this), and right away when a device is plugged in or the PC wakes up.

## Smart Battery Alerts (experimental)

You can set a **per-device low-battery notification threshold** from each device's
submenu: **Battery alerts** → *Use default (15%)*, *Disable notifications*,
or one of 5%, 10%, 15%, 20%, 25%, 30%, 35%, 40%. The existing 15% default stays
unchanged unless you choose an override. Settings are stored by device key in
the current user's Windows registry (`Software\WirelessBattery`, `AlertThresholds`).

An alert is triggered only from a real, fresh 0–100% battery reading when the
device is connected, awake and not charging. An alert is issued once, and will
re-arm after the level climbs more than 10 percentage points above the chosen
threshold (or when charging begins). Missing readings or sleeping devices do
not retrigger alerts. Hidden devices don't produce notifications. Disabling
notifications for one device does not hide it or change its percentage.

Both taskbar-widget and tray-icon modes support notifications. Since Windows
needs a notification-area icon to display a legacy balloon, widget mode may
show a temporary notification icon for up to one minute after an alert.
Widget colours and the low-battery highlight still use the project's
original global 15% threshold, independently of the notification setting.

## Install

[![Latest release](https://img.shields.io/github/v/release/ahizechi/Battery?label=latest%20release&style=for-the-badge)](https://github.com/ahizechi/Battery/releases/latest)

1. Download **`BatteryHub-Setup-<version>.exe`** from [this fork's latest release](https://github.com/ahizechi/Battery/releases/latest).
2. Run it and click through. It needs **no administrator rights** and **nothing else installed**: no
   runtimes, no drivers, no vendor apps. HeadsetControl comes bundled.

The installer adds a Start menu entry and an uninstaller (*Settings → Apps*), and can start the app
when you sign in. Prefer no installer? Download `BatteryHub-<version>-portable.zip`, unzip it
anywhere and run `BatteryHub.exe`.

**Notes:**
- **SmartScreen warning:** the exe is not code-signed, so Windows may say "Windows protected your PC". Click *More info → Run anyway*, or build it yourself.
- **Tray-icon mode on Windows 11:** new tray icons go to the `^` overflow. Drag them onto the taskbar or enable them in *Settings → Personalization → Taskbar → Other system tray icons*.
- **Keeping the app away from a device:** if a device misbehaves while the app is running, list it in the registry string `IgnoreDevices` under `HKCU\Software\WirelessBattery` as `VID:PID` pairs (hex, separated by commas, e.g. `28BD:0947, 1234:ABCD`; `--dump` shows the ids). The app then never opens it. Drawing tablets are skipped automatically.

## Updates on Windows

This fork updates only from its own repository, `ahizechi/Battery`, and only trusts releases signed
with its own key (the original project's key is not built in). Official stable Windows builds check
the latest published stable release in the background, 30 seconds after startup and then once a
day while the app is running; a failed check also waits a day. Open the
menu to see **Update available: vX.Y.Z...**, or choose **Check for updates...** yourself.
Update information appears only in this menu: battery icons, percentages and tooltips stay the
same, and automatic checks never show a notification or dialog. Turn off
**Automatically check for updates** in the menu to disable background checks.

For an installed copy, click the update entry and **Download and install**. Nothing is installed
without that click. The app downloads the release's `SHA256SUMS.txt` and its signature, checks the
signature against the maintainer's public key built into the app, downloads the Windows installer,
and runs it only if its SHA-256 is the one in the signed list (and the one GitHub lists for it). It
then updates the existing installation. Settings and startup preference are
preserved, and the app reopens after installation. An installation for all users can ask for
administrator permission. **Release notes** opens the official release page; **Later** keeps
your current version. Portable copies offer the release page for downloading the ZIP manually.

Checks contact GitHub's public API without an account or token. No device or battery information
is sent. Offline failures and GitHub rate limits leave battery monitoring running; a previously
found release stays in the menu until a successful check supersedes it or it is installed.
Cancelling a download waits for the current network operation to finish its finite timeout.
The installer never restarts Windows automatically.

**Why updates are safe to install:** the signing key never goes to GitHub. The maintainer signs
each release's checksum list on their own PC (see [docs/releasing.md](releasing.md)), so
even someone who took over the GitHub account or its build workflow could not publish an update
the app would install. A release without a valid signature is shown in the menu but can only be
downloaded by hand. The signature does not replace Windows' own SmartScreen or UAC prompts.

Install the first release containing this feature manually once. Development/test builds disable
official update checks so local changes cannot be replaced accidentally. Updates for Linux and
automatic replacement of portable copies are deferred.

## Command line

```
BatteryHub.exe --list     # print every device and its battery level, then exit
BatteryHub.exe --json     # print a JSON array of devices and battery levels, then exit
BatteryHub.exe --dump     # every HID collection, HID devices that could not be opened, all USB devices (also ones
                               # without a HID interface) and Bluetooth devices; also saved to %TEMP%\BatteryHub-dump.txt
BatteryHub.exe --report   # open the "Request support for a device" window
BatteryHub.exe --demo     # sample devices of every brand instead of real ones (screenshots, checking colors)
BatteryHub.exe --quit     # close the running instance
```

`--list` and `--json` read only the sources switched on under *Read from* (HeadsetControl is off unless you
switched it on).

The Linux build uses the same flags with `wireless-battery` instead of `BatteryHub.exe`, and
also accepts `--help`. `--json` prints a stable array that scripts and bar widgets can parse:

```json
[{"key":"kysona:373B:1085","name":"CX Wireless mouse","via":"2.4 GHz receiver","kind":"mouse",
  "percent":85,"level":"","detail":"","charging":false,"asleep":false,"connected":true,
  "millivolts":4029,"graceSec":1800}]
```

`graceSec` is how long the app keeps showing the last known level while the device is asleep.
Adding new fields is backwards compatible.

**Device not shown?** Right-click the widget or tray icon and choose **Request support for a device...**. Pick the device, check
what will be sent (it is shown word for word: the device name, its type, its USB/HID ids and interface layout, and your optional
name and notes) and click **Send**. It goes on the public list at [krisgfx.com/tools](https://krisgfx.com/tools/#requested) and
opens or updates a GitHub issue. **Copy full report** puts the complete `--dump` on the clipboard for the issue.
Report data is sent only when you click Send. Official Windows builds can also contact GitHub
to check for updates, as described in [Updates on Windows](#updates-on-windows); update checks
do not send device or battery information.

## Build

You need [MinGW-w64](https://www.mingw-w64.org/), e.g. from [MSYS2](https://www.msys2.org/) (`pacman -S mingw-w64-x86_64-gcc make`).

```
make                                      # development build, version 0.0.0
build.bat                                 # plain cmd with g++ and windres on PATH
make VERSION=1.2.3 CHANNEL=test            # versioned build, official updates disabled
build.bat 1.2.3 test                       # equivalent versioned cmd build
make test VERSION=1.2.3 CHANNEL=test       # device and updater regression tests
```

To build the installer and portable zip, install [Inno Setup](https://jrsoftware.org/isinfo.php),
first build with the intended version (for example `make VERSION=1.2.3 CHANNEL=test`), then run
`powershell -File installer\build-installer.ps1 -Version 1.2.3`. Packaging rejects a version that
differs from the executable. It downloads the pinned
HeadsetControl release and verifies its checksum. GitHub Actions does all of this automatically:
pushing a tag like `v1.0.0` publishes a release with the installer, the portable zip and the
HeadsetControl source (as its GPL license requires).

Windows builds also need PowerShell for generating the shared executable/resource version.
This repository's (`ahizechi/Battery`) version tags build with `CHANNEL=stable`; local builds
default to `dev`. Other forks and pull requests do not enable official updates automatically.

`BatteryHub.exe` on its own also works. Without `headsetcontrol.exe` next to it (or on `PATH`) you
just lose the extra headsets.

## Linux (experimental)

A Linux build sits alongside the Windows app. It uses the same vendor drivers with a hidraw-based
HID backend and a GTK4 StatusNotifierItem tray icon; the Windows code path is unchanged.
All Linux-only files are in `linux/` and `src/linux/`, and `build-linux.sh` builds it (Windows keeps using `build.bat`).

| | Fedora | Ubuntu / Debian |
|---|---|---|
| Compiler & make | `sudo dnf install gcc-c++ make` | `sudo apt install g++ make` |
| GTK4 + Cairo | `sudo dnf install gtk4-devel cairo-devel` | `sudo apt install libgtk-4-dev libcairo2-dev` |

No extra HID library is needed (hidraw is used directly).

```sh
./build-linux.sh
./build-linux.sh test         # unit test for the HID report-descriptor parser
./wireless-battery --list     # print detected levels and exit
./wireless-battery --dump     # list HID collections
./wireless-battery --quit     # close the running tray instance
```

HID access needs a udev rule, otherwise the app starts but finds no devices:

```sh
sudo cp linux/99-wireless-battery.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# log out and back in
```

The tray icon works natively on KDE Plasma; GNOME needs an AppIndicator host such as
`gnome-shell-extension-appindicator`. The optional launcher is `linux/wireless-battery.desktop`
(install it plus `linux/wireless-battery.svg` and the binary into `~/.local/`).

Phase 1 covers Logitech, Razer, SteelSeries, Corsair, HyperX, PlayStation, Cooler Master, Glorious,
Finalmouse and the shared Kysona/Pulsar/ATK/G-Wolves protocols. Bluetooth/UPower fallbacks, HeadsetControl and
the remaining vendors are planned follow-up phases.

## Limitations

- **Crowded taskbars:** with a very full centered Windows 11 taskbar, the app buttons can reach the widget. On Windows 10 the task-button area is narrowed automatically.
- **Phones:** read from a file Phone Link writes for the Start menu companion card, which is not a documented API and may change with Phone Link updates. The phone shows while Phone Link reports it as connected; a card older than 3 hours is shown dimmed. The card also says when the phone is charging, which shows as a green level bar.
- **Sleeping devices:** a device that is asleep may not answer. Its last value stays visible, dimmed. Logitech receivers drop sleeping devices from the link, so their last value is kept for 30 minutes.
- **HECATE G2 Wireless 7.1 headset** (2.4 GHz dongle `35BB:A009` / `A209`): no battery query for its dongle is documented yet, so it is not read. A USB capture while the HECATE software shows the headset battery (see CONTRIBUTING.md) is the way to add it.
- **Keychron keyboards:** with stock firmware they only report their battery over Bluetooth (shown via the Bluetooth support). Over the 2.4 GHz receiver or the cable they need firmware with the battery patch ([docs/keychron_keyboard_firmware/README.md](keychron_keyboard_firmware/README.md)); updating the keyboard from Keychron Launcher removes it. Keychron mice are supported.
- **Baseus AeQur GH02 headset:** the 2.4 GHz dongle (`10D6:4802`, Actions chip) only carries audio, media keys and the chip's firmware-update channel, not the battery. Connect the headset over Bluetooth to see its level (shown via the Bluetooth support when Windows reports one).
- **Weikav Lucky65 v1 keyboard:** the firmware does not report its battery over the 2.4 GHz dongle (`0603:0351`, Novatek chip). The keyboard has no battery-check shortcut, and the official configurator only connects over the USB cable, not through the dongle, so there is no battery reading to show.
- **Rapoo:** supported VT mice push their status, so a refresh can wait up to 4 seconds per matching interface. Sleeping mice may stay dimmed with their last reading for up to 30 minutes. Support is for Nordic 54L report `07` and legacy VT3 Pro `BB`; CH584 report `08`, office MT/M-series mice and other firmware protocols are not covered by this driver.
- **Rapoo:** supported VT mice push their status, so a refresh can wait up to 4 seconds per matching interface. Sleeping mice may stay dimmed with their last reading for up to 30 minutes. Support is for Nordic 54L report `07` and legacy `BB` (VT3 Pro, VT1 Pro Max); CH584 report `08`, office MT/M-series mice and other firmware protocols are not covered by this driver.
- **GameSir Cyclone Pro:** the dongle carries no battery level, so it is read from the pad over Bluetooth LE. Pair the pad with Windows once (Settings > Bluetooth & devices > Add device > Bluetooth > `GameSir-Cyclone Pro`), and keep playing through the dongle. The PC needs Bluetooth. The level refreshes every 2 minutes, and there is no percentage while charging. Without pairing, the dongle shows as an Xbox controller at "Full". Details in [docs/gamesir_cyclone_pro.md](gamesir_cyclone_pro.md).
- **RAWM:** the mouse is asked through its receiver, so a sleeping mouse does not answer; it stays dimmed with its last reading for up to 30 minutes and adds about a second to a refresh while it sleeps. Only the Leviathan V4 on the RAWM HS Receiver (`1915:2346`) is tested; the other models are taken from RAWM HUB's device table. The Leviathan V4 GT and NYX60 (VID `373E`, RAWM HUB 3.0) and the RAWM HS keyboards use other protocols and are not covered.
- **Flydigi:** this driver targets Vader 5 Pro and Apex 5; Bluetooth and other Flydigi models (for example Apex 6) are not covered. Percentages represent coarse battery levels. Apex 5 was tested on real hardware over the 2.4 GHz receiver and a USB cable; Vader 5 Pro follows SDL and Windows hardware traces.
- **Not supported yet:** MSI wireless mice other than the VERSA 300 ELITE and the CLUTCH GM31 / GM41, MSI wireless keyboards, Razer Kraken headsets and the Barracuda models other than the Barracuda X (2022) on `1532:0550`, HyperX Pulsefire mice other than the Haste 2 Wireless / Haste 2 Core Wireless and the Xbox Wireless Headset on the Xbox adapter: their battery protocols are not documented. The Xbox Wireless Headset works over Bluetooth. A USB capture from an owner (see CONTRIBUTING.md) is the way to add them.
- **Not supported yet:** MSI wireless mice and keyboards, Razer Kraken headsets and the Barracuda models other than the Barracuda X (2022) on `1532:0550`, other HyperX Pulsefire mice and the Xbox Wireless Headset on the Xbox adapter: their battery protocols are not documented. The Xbox Wireless Headset works over Bluetooth. A USB capture from an owner (see CONTRIBUTING.md) is the way to add them.
- **Other devices:** some 2.4 GHz devices use undocumented protocols and are not supported yet. See [CONTRIBUTING.md](../CONTRIBUTING.md) for how to help add one.

## How it works

Each vendor driver in [`src/drivers/`](../src/drivers) recognises its devices by USB vendor and product ID, and only ever sends read-only battery queries. Unknown devices are never written to. The generic fallbacks (HID battery usage, Bluetooth, XInput, HeadsetControl) only look at devices no vendor driver has claimed.

The taskbar widget is a layered child window of `Shell_TrayWnd`, placed left of `TrayNotifyWnd`. If Explorer restarts, it is re-created.

## Translations

Every language is a plain UTF-8 text file in [`lang/`](../lang): English on the left, the translation on the right.

```
@code = it
@name = Italiano
Refresh now = Aggiorna ora
{name} is at {percent}% = {name} è al {percent}%
```

- **Use your own language right away:** choose *Language → Add your own language...* in the menu. It opens `%APPDATA%\WirelessBattery\lang` with `_template.txt`, which lists every phrase. Copy it to e.g. `it.txt`, fill in `@code`, `@name` and the translations, and the language appears in the menu (no restart needed). A file there with the same `@code` as a built-in language replaces its phrases, so you can also fix a single word locally.
- **Share it with everyone:** open a pull request for the relevant file under this repository’s [language directory](../lang/). The build includes files in that directory automatically.
- Keep `{placeholders}` as they are, and add `@rtl = 1` for right-to-left scripts. Phrases left empty stay English, so a partial translation is fine.
- The `--list` and `--dump` output stays English on purpose: it is pasted into GitHub issues.
