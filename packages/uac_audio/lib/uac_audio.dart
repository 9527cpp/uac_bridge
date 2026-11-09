import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'uac_audio_bindings_generated.dart' as bindings;

/// One enumerated capture or playback endpoint.
class AudioDevice {
  const AudioDevice({required this.index, required this.name});

  final int index;
  final String name;

  bool get isUac {
    final lower = name.toLowerCase();
    return lower.contains('uac') || lower.contains('gadget');
  }
}

/// Lists sound devices and runs up to four capture-to-playback loops.
class UacAudio {
  List<AudioDevice> playbackDevices = const [];
  List<AudioDevice> captureDevices = const [];

  /// Reloads the device lists. Returns an empty string on success.
  String refresh() {
    final code = bindings.uac_refresh();
    playbackDevices = _read(playback: true);
    captureDevices = _read(playback: false);
    if (code != 0) {
      return lastError();
    }
    return '';
  }

  /// Starts route [routeId] (0 to 3). Returns an empty string on success.
  String startRoute({
    required int routeId,
    required int captureIndex,
    required int playbackIndex,
  }) {
    final code = bindings.uac_route_start(routeId, captureIndex, playbackIndex);
    if (code != 0) {
      return lastError();
    }
    return '';
  }

  void stopRoute(int routeId) => bindings.uac_route_stop(routeId);

  bool routeActive(int routeId) => bindings.uac_route_active(routeId) != 0;

  /// Capture peak for [routeId], from 0 to 32767.
  int routePeak(int routeId) => bindings.uac_route_peak(routeId);

  String lastError() => _readString(bindings.uac_last_error);

  List<AudioDevice> _read({required bool playback}) {
    final count = bindings.uac_device_count(playback ? 1 : 0);
    final devices = <AudioDevice>[];
    for (var index = 0; index < count; index++) {
      final name = _readIndexed((out, length) {
        return bindings.uac_device_name(playback ? 1 : 0, index, out, length);
      });
      if (name.isEmpty) {
        continue;
      }
      devices.add(AudioDevice(index: index, name: name));
    }
    return devices;
  }

  String _readString(int Function(Pointer<Utf8> out, int outLen) read) {
    return _readIndexed(read);
  }

  String _readIndexed(int Function(Pointer<Utf8> out, int outLen) read) {
    final buffer = calloc<Uint8>(512);
    try {
      read(buffer.cast<Utf8>(), 512);
      return buffer.cast<Utf8>().toDartString();
    } finally {
      calloc.free(buffer);
    }
  }
}
