# Credits

Battery is a fork of BatteryHub. These are the people and projects whose work it is built on.

## Credits

- **BatteryHub** by **KrisGFX** and its contributors: the app this fork is built on, nearly all of its
  code and documentation, and the device research credited below. Its GitHub repository is no longer
  online. Support KrisGFX on [Ko-fi](https://ko-fi.com/krisgfx) or [PayPal](https://www.paypal.com/paypalme/krisgfx).
- **[TraySense](https://github.com/DMTWEAKS/TraySense)** by DMTWEAKS: the idea of showing hardware
  usage and temperatures on the taskbar. This fork's hardware readings are written separately (TraySense
  is AGPL-3.0); none of its code is used.
- **[HeadsetControl](https://github.com/Sapd/HeadsetControl)** by Sapd and contributors (GPL-3.0),
  bundled for extra headsets.
- App icon by [nik-txt](https://github.com/nik-txt) ([`res/app.svg`](../res/app.svg)).
- **[Geist](https://github.com/vercel/geist-font)** by Vercel and basement.studio (SIL Open Font License 1.1):
  the taskbar strip's numbers ([`res/Geist-Variable.ttf`](../res/Geist-Variable.ttf); licence in
  `installer/THIRD_PARTY_NOTICES.txt`).

The protocols come from these projects' research:

- [Google Fast Pair specification](https://developers.google.com/nearby/fast-pair/specifications/extensions/batterynotification): left / right / case battery broadcasts of Fast Pair earbuds
- [HeadsetControl Astro drivers](https://github.com/Sapd/HeadsetControl/blob/master/lib/devices/logitech_astro_a50.hpp), [eh-fifty](https://github.com/tdryer/eh-fifty), and [AstroBar](https://github.com/nichtlegacy/AstroBar): Astro A50 X / Gen 5 / Gen 4 / Gen 3 / A20 battery protocols; [OpenAstroCC](https://github.com/BenStein1/OpenAstroCC): A50 Gen 3 `0x72` status read

- [mouse-battery-monitor](https://github.com/stuffz/mouse-battery-monitor) (stuffz): VAXEE battery and charging commands ([protocol documentation](https://github.com/stuffz/mouse-battery-monitor/blob/main/docs/VAXEE.md))

- [HeadsetControl](https://github.com/Sapd/HeadsetControl): SteelSeries, Corsair, HyperX, Logitech and Sony INZONE headsets
- [inzone-buds-battery-tray](https://github.com/kinako19/inzone-buds-battery-tray) (kinako19) and [inzone-buds-battery-tray](https://github.com/takamachi66/inzone-buds-battery-tray) (takamachi66): Sony INZONE Buds
- [OpenRazer](https://github.com/openrazer/openrazer): Razer
- [Solaar](https://github.com/pwr-Solaar/Solaar) and the Logitech HID++ 2.0 documents: Logitech
- [g435-battery-percentage](https://github.com/portyporty/g435-battery-percentage) and [logitech-g435-battery](https://github.com/deddroid/logitech-g435-battery): Logitech G435 (`0ADF` dongle)
- [rivalcfg](https://github.com/flozz/rivalcfg): SteelSeries mice (Rival Pro from its pull request #294)
- [glorious-indicator](https://github.com/ikajdan/glorious-indicator) and [glorious-ctl](https://github.com/louis4craft/glorious-ctl): Glorious
- Hitscan's web configurator ([control.hitscan.com](https://control.hitscan.com)): Hitscan
- Linux `hid-kysona` (Lode Willems): Kysona, Pulsar and ATK
- ATK's web configurator ([hub.atk.pro](https://hub.atk.pro)): ATK mice and the N9 Pro / N9 Ultra headsets
- [PulsarBattery](https://github.com/darthsoup/PulsarBattery): Pulsar X2 V3 / X2 V3 eS
- [HaloBattery](https://github.com/HeyOkay/HaloBattery) PR #350 and [mouse-battery](https://github.com/qwr79/mouse-battery): ATK / VXE NK NANO dongle
- [HaloBattery](https://github.com/HeyOkay/HaloBattery) (MIT): Audeze Maxwell, NZXT Lift Elite, Poly BT600, Razer Barracuda Pro, SteelSeries Arctis Nova Elite, ATK V75X and Endgame Gear XM2we, ported from its providers (each driver names its source)
- [HaloBattery](https://github.com/HeyOkay/HaloBattery) `providers/turtlebeach.py` and [halobattery-stealth700](https://github.com/PaulRichez/halobattery-stealth700) (PaulRichez, MIT): Turtle Beach Stealth 700 Gen 3; [Neap](https://github.com/lykosapps/Neap) (findings only): the Turtle Beach Swarm II device family
- G-Wolves' web driver ([mouse.xyz](https://mouse.xyz)): G-Wolves models and their protocols
- [BatteryWatch](https://github.com/B1Chat/BatteryWatch): CHERRY keyboards on the 2.4 GHz dongle (frame format and battery query)
- Keychron Launcher ([launcher.keychron.com](https://launcher.keychron.com)): Keychron mice
- [NinjaForce](https://www.ninjaforce.co/) and [ninjutso-battery-tray](https://github.com/DrStopor/ninjutso-battery-tray): Ninjutso Ten / Ten Air feature queries
- [nape_pro](https://github.com/nvalis/nape_pro) and [KeychronNapeBatteryWidget](https://github.com/gtun/KeychronNapeBatteryWidget): Keychron Nape Pro on the Link-KM receiver
- LAMZU Aurora ([lamzu.net](https://www.lamzu.net)): LAMZU mice
- [vole](https://github.com/johanneszab/vole) and [Endgame_Gear_Web_Configurator](https://github.com/Janlhin/Endgame_Gear_Web_Configurator): Endgame Gear 4K mice
- [mouse-gauge](https://github.com/djnawara/mouse-gauge) and [HaloBattery](https://github.com/Waturus/HaloBattery): WLMouse
- [OpenMouse mouse-protocol](https://github.com/OpenMouse-Project/mouse-protocol) and [FinalmousePollingRateSwitcher](https://github.com/xBambooz/FinalmousePollingRateSwitcher): Finalmouse UltralightX / Starlight X (from Finalmouse's xpanel)
- [redragon-battery-omarchy](https://github.com/EliottV17/redragon-battery-omarchy) and [g49-web-control](https://github.com/pingpingjinping/g49-web-control): Redragon ids
- [m913-ctl](https://github.com/Qehbr/m913-ctl) and [redragon-m913-battery-tray](https://github.com/Doomguy-BFG9000/redragon-m913-battery-tray): Areson `25A7` frame layout and `GetPower` command
- [peripheral-battery-tray](https://github.com/SteveWang92/peripheral-battery-tray) / [G-Helper](https://github.com/seerge/g-helper): ASUS ROG
- [peripheral-battery-tray](https://github.com/SteveWang92/peripheral-battery-tray) / [G-Helper](https://github.com/seerge/g-helper): ASUS ROG; [pelta-control](https://github.com/jimm0thy/pelta-control) / [BatteryWatch](https://github.com/B1Chat/BatteryWatch): ROG Pelta
- [zowie-battery](https://github.com/yleshchuk/zowie-battery) and [zowie-battery-tray](https://github.com/IgnacioBarb/zowie-battery-tray): ZOWIE EC-CW and DW
- RK's web driver ([drive.rkgaming.com](https://drive.rkgaming.com)): Royal Kludge MG6 Pro
- [MCHOSE-Monitor](https://github.com/Jstdi001/MCHOSE-Monitor): MCHOSE G3 V2
- [mchose-a5-pro-max-web-driver](https://github.com/z750sasr/mchose-a5-pro-max-web-driver) (MIT), [HaloBattery](https://github.com/HeyOkay/HaloBattery) (MIT), [mchose-macos](https://github.com/Klegus/mchose-macos) (MIT) and [OpenMouse mouse-protocol](https://github.com/OpenMouse-Project/mouse-protocol) (docs): MCHOSE A5 Pro / A5 Pro Max (first generation, VID `2023`)
- [LGSTrayBattery-MotospeedX6](https://github.com/Jacobz888/LGSTrayBattery-MotospeedX6), [omarchy-delux-m900](https://github.com/AeonArcHQ/omarchy-delux-m900), [aj179-linux](https://github.com/Rockeyxx/aj179-linux): Motospeed, Delux, Ajazz
- [AreYouLowBattery](https://github.com/ryan-mant/AreYouLowBattery): CompX receiver reports for Delux M900 Pro
- [ajazz-aj139-macos](https://github.com/werquess/ajazz-aj139-macos) and [AjazzAJ139V2](https://github.com/nico2525nn/AjazzAJ139V2): Ajazz AJ139 V2
- [Incott-Manager](https://github.com/OmarK6/Incott-Manager), [incott-mouse-app](https://github.com/HeHaoren/incott-mouse-app) and [MouseBattery](https://github.com/FelixNg21/MouseBattery): Incott
- The Wraith web driver ([w1.software](https://www.w1.software/)): Wraith W1
- OpenRazer PR #2794 and [omarchy-blackshark-v3](https://github.com/ReedME/omarchy-blackshark-v3): BlackShark V3
- OpenRazer PR #2862 and [HaloBattery](https://github.com/HeyOkay/HaloBattery): BlackShark V2 Pro (2023)
- [razer-barracuda-x-battery](https://github.com/mehdibouchami/razer-barracuda-x-battery) and [barracuda-x-battery](https://github.com/nightdevx/barracuda-x-battery): Razer Barracuda X (2022) on the `1532:0550` receiver; [razer-barracuda-battery-tray](https://github.com/KhromotozzDevOut/razer-barracuda-battery-tray): framing of the `1532:0552` receiver
- [OpenRazer PR #2899](https://github.com/openrazer/openrazer/pull/2899): BlackShark V3 X
- [HyperXWM](https://github.com/Medprj/HyperXWM): HyperX Cloud III Wireless
- [HyperHeadset](https://github.com/LennardKittner/HyperHeadset): HyperX Cloud III S, Cloud II Core, Cloud Flight S
- [HyperBatteryMonitor](https://github.com/davidsantanaalves/HyperBatteryMonitor): HyperX Cloud Stinger Core Wireless
- [BatteryTray](https://github.com/qu1ncky/BatteryTray): Rongyuan and Xenta receivers (Attack Shark and others)
- [sonix-keyboard-studio](https://github.com/donglover69/sonix-keyboard-studio): SONiX keyboards on their 2.4G dongle (AJAZZ AK820 MAX, AK980 V2, AULA F108 Pro); [ak820-rs](https://github.com/deddroid/ak820-rs) and [ak820-studio](https://github.com/usharik/ak820-studio): AJAZZ AK820 Pro receiver `0C45:FDFD`
- [openajazz](https://github.com/shrey715/openajazz) and [Kraber](https://github.com/AkashPanja/Kraber): Semico `1A2C` (`AA 55 CC 33` / `0x0E`) status protocol for Attack Shark X98 and AJAZZ AK820 Max Plus
- [coolermaster-usb-protocol](https://github.com/syntactic-spice/coolermaster-usb-protocol) and [MM731-Controller](https://github.com/riVFerd/MM731-Controller): Cooler Master MM712 and MM731
- [GamesirBatteryTray](https://github.com/WWWyyy452/GamesirBatteryTray) and [Gamesir-Nova-2-Lite-Battery-Detection](https://github.com/AJAY0406/Gamesir-Nova-2-Lite-Battery-Detection): GameSir Nova
- [GameSirBattery](https://github.com/bpbp-boop/GameSirBattery): GameSir Cyclone Pro Bluetooth LE "base info" battery request
- [jabridge](https://github.com/Watchdog0x/jabridge): Jabra GNP protocol (Link 380 / 390 dongles)
- [xd5-go-web](https://github.com/jarjut/xd5-go-web): Fantech Helios Go XD5
- Linux `hid-playstation`: DualShock 4 / DualSense report layout
- [omarchy-aula-SC580](https://github.com/Crucialjun/omarchy-aula-SC580): AULA SC580 / SC580X vendor protocol
- RAWM HUB web app ([rawmtech.com/hub.html](https://www.rawmtech.com/hub.html), Hub Web 2.0) and its device table: RAWM mice and the SADES Dagger
- [SDL's Flydigi HID driver](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_flydigi.c) and [HaloBattery issue #191](https://github.com/HeyOkay/HaloBattery/issues/191): Vader 5 Pro and Apex 5 query framing and battery layout
- [SDL's 8BitDo HID driver](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_8bitdo.c): 8BitDo Ultimate 2 battery byte
- BEITONG iControl (its KP20 and KP40D pages and the KP20 receiver's USB traffic): BEITONG KP20 / KP40D battery query and payload scrambling
- [SDL's 8BitDo HID driver](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_8bitdo.c) and [8bitdo-ultimate2-linux](https://github.com/MiguelSanzBr/8bitdo-ultimate2-linux): 8BitDo battery byte and vendor HID protocol
- JBL QuantumENGINE (its HIDV3 device-info messages): JBL Quantum headsets
- [HaloBattery](https://github.com/HeyOkay/HaloBattery/blob/main/docs/protocols.md): Corsair Sabre V2 Pro Ultralight, Compx protocol on `FF02:0002`
- [MouseDock](https://github.com/late-autumn-gif/mousedock) (MIT) and [BrimDeck](https://github.com/MoFeng2223/BrimDeck): Scyrox V12M profile read
- JBL QuantumENGINE: HIDV1 connection and battery reports for the Quantum 350, and HIDV3 device-info messages for newer Quantum headsets
- [jbl-quantum-linux](https://github.com/Tymon3310/jbl-quantum-linux/blob/main/docs/QUANTUM610_PACKETS.md): JBL Quantum 610 HIDV1 battery and link reports, and the Quantum lineup (360 as a HIDV1 dongle model)
