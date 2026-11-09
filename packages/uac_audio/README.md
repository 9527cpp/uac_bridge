# uac_audio

本仓库自己的 FFI 音频库，给上层的 `uac_bridge` 用。它枚举采集和播放设备，并最多打开 4 路「采集 → 播放」环路。

设备访问使用同目录下的第三方头文件 `src/miniaudio.h`（miniaudio 0.11.22）。`src/uac_audio.c` 和 `lib/` 里的 Dart 绑定是本仓库写的。

原生代码在 `hook/build.dart` 里编译，由依赖这个包的 Flutter 应用在构建时自动调用。
