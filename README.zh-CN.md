# 无线设备电量监测 · 简体中文版

[Wireless Battery](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor) 的简体中文使用说明。

在 Windows 10/11 任务栏显示无线鼠标、耳机、键盘和手柄的电量。此版本汉化菜单、悬浮提示、低电量通知、电量等级、连接方式、通用设备名称及命令行提示；品牌、具体型号、设备标识和协议字段保持原样。中文为默认语言，无需设置。

## 使用

解压便携包，双击 `WirelessBattery.exe`。不需要安装，也不需要管理员权限。程序运行在任务栏，点击电量区域即可打开菜单。

- **在任务栏显示（图标 + 电量）**：默认模式。
- **显示为托盘图标**：每个设备一个图标；Windows 11 可能将新图标收进右下角的折叠区。
- **点击设备可显示或隐藏**：勾选的设备会显示。
- **隐藏仅有历史电量或已休眠的设备**：隐藏暂时无法读取最新电量的设备。
- **立即刷新**：重新读取；默认每 60 秒刷新，插拔设备或唤醒电脑后也会更新。
- **开机自启动**：当前用户登录 Windows 时启动此程序。
- **退出**：关闭程序。

红色电量表示剩余不超过 15%；蓝色闪电表示充电中；暗淡文字表示暂时无法获取新读数，正在显示上次记录。部分设备只提供“偏低、良好、充足”等等级，不能视为精确百分比。

## 支持范围与限制

**迈从 MCHOSE G87**：通过 `41E4:2001` 的 2.4 GHz 接收器读取电量，已在真实设备上实测返回 55%～56%。只适用于该设备编号；蓝牙、有线模式及 G87 V2 未验证。充电标志尚未确认，因此暂不显示 G87 充电状态；休眠时保留上次读数最多 30 分钟。

协议资料来源：[marsuyone/mchose-battery-tray](https://github.com/marsuyone/mchose-battery-tray)，参考提交 `7206b460` 的 G87 查询说明。驱动为独立 C++ 实现，只发送电量查询，不修改键盘设置。查询使用 FF02 接口；FF04 的容量值不作为实时电量。

支持 Logitech、Razer、SteelSeries、Corsair、HyperX、雷柏、ROG、LAMZU、AULA、部分国产无线设备，以及 PlayStation / Xbox 手柄。完整型号、协议和实测情况见 [原版说明](README.md#supported-devices)。支持某个品牌不代表支持其所有型号。

蓝牙设备需要 Windows 本身能读取电量。部分设备休眠后不会响应，部分 2.4 GHz 接收器不提供电量。任务栏组件仅显示在主显示器上；并非所有支持的硬件均已实测。

便携包附带 HeadsetControl 4.3.0，用于补充耳机支持。将其与主程序放在同一目录。第三方工具自身的诊断输出保持原版。

## 命令行

```powershell
.\WirelessBattery.exe --list   # 列出设备及电量后退出
.\WirelessBattery.exe --dump   # 列出 HID 接口，供设备适配排查
.\WirelessBattery.exe --quit   # 关闭正在运行的实例
```

控制台输出使用 Unicode，重定向到文件时使用 UTF-8。`--dump` 和 `--list` 可能包含设备标识，分享排查记录前可自行遮盖。

## 从源码编译

安装 MinGW-w64，将 `g++`、`windres` 加入 PATH，然后运行 `build.bat`；MSYS2 环境也可以运行 `make`。源文件采用 UTF-8，构建脚本已显式指定编码。`src/localization.h` 集中管理设备状态与连接方式的显示翻译，驱动内部原始值不变。

`src/drivers/xinput.cpp` 对部分 MinGW 版本的 WinRT `boolean` 类型冲突增加了局部兼容处理，不修改系统工具链文件。

新版 WinLibs 会自动嵌入默认清单，与项目的 DPI 清单冲突；此类环境请运行 `powershell -File scripts\build-windows.ps1`。

安装器源码也已汉化，需要 Inno Setup 6.5.0 或更新版本才能构建。中文语言文件来自 [Inno Setup 官方仓库](https://github.com/jrsoftware/issrc/blob/main/Files/Languages/ChineseSimplified.isl)，保留其译者署名：

```powershell
powershell -File installer\build-installer.ps1 -Version 1.0.0-zh-CN
```

安装器汉化尚需单独编译和视觉验证。

## 许可证与来源

遵循原项目 [GPL-3.0-or-later](LICENSE)。重新分发二进制时，应提供对应源码及许可证。HeadsetControl 的声明见 [第三方声明](installer/THIRD_PARTY_NOTICES.txt)，分发时也需提供其对应源码。原作者和第三方项目的致谢保留在英文 README 中。
