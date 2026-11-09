# UAC 环路

把一块声卡的采集口接到另一块声卡的播放口。用来在 Windows、Linux、macOS、Android 上把 UAC2 虚拟声卡和本机麦克风、喇叭串起来。

整个程序在这一个目录里：

- `lib/`：Flutter 界面。
- `packages/uac_audio/`：本仓库自己的音频库。Dart 负责选设备和开关环路，`src/uac_audio.c` 用 miniaudio 打开双工设备并把采集拷到播放。
- `packages/uac_audio/src/miniaudio.h`：第三方单头文件库 [miniaudio](https://github.com/mackron/miniaudio) 0.11.22，许可证是公有领域或 MIT-0，声明在文件末尾。

界面有两路，下拉框可以改成任意采集设备和任意播放设备：

- 听到设备：UAC2 麦克风 → 本机喇叭。
- 对着设备说：本机麦克风 → UAC2 扬声器。

## 编译

需要 Flutter 3.44（Dart 3.12）或兼容版本。在本目录执行：

```bash
flutter pub get
flutter run -d linux
flutter run -d windows
flutter run -d macos
flutter run -d android
```

Linux 需要 clang、cmake、ninja、gtk3 开发包。Windows 用 Visual Studio 的桌面 C++ 工具链。macOS 用 Xcode。Android 需要已接受协议的 Android SDK，并授予麦克风权限。

原生库在 `flutter run` / `flutter build` 时由 `packages/uac_audio/hook/build.dart` 编译，不需要单独编 `uac_audio`。
