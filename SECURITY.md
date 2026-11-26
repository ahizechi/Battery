# Security

This page covers Battery, the fork of BatteryHub by KrisGFX and its contributors.

## Reporting a problem

Please report security problems privately: open the repository's **Security** tab and choose
**Report a vulnerability**. Do not open a public issue for them.

Most relevant here:

- the updater: anything that could make the app install a file that is not this fork's signed release
  (`src/update_*.cpp`, `scripts/sign-release.ps1`, `docs/releasing.md`);
- the hardware readings and device drivers: anything that writes to a device or opens more than it reads.

If the problem also affects BatteryHub, note that in the private report so it can be coordinated with the original maintainers.

## What the app does and does not do

- The automatic update check contacts GitHub for this repository. Updates require a `SHA256SUMS.txt`
  signed by this fork’s release key, whose public half is in `src/update_public_key.h`. The device
  support window can also submit a user-reviewed report to the original developer’s service when
  the user selects **Send**. See the [guide](docs/guide.md#command-line).
- Devices: drivers send read-only queries. The app never writes settings, lighting or firmware.
- No administrator rights, no service, no driver.
