import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:uac_audio/uac_audio.dart';

void main() {
  runApp(const UacBridgeApp());
}

class UacBridgeApp extends StatelessWidget {
  const UacBridgeApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'UAC 环路',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF0F6E56)),
        useMaterial3: true,
      ),
      home: const BridgePage(),
    );
  }
}

class _RouteSpec {
  const _RouteSpec({
    required this.id,
    required this.title,
    required this.detail,
    required this.capturePrefersUac,
  });

  final int id;
  final String title;
  final String detail;
  final bool capturePrefersUac;
}

class BridgePage extends StatefulWidget {
  const BridgePage({super.key});

  @override
  State<BridgePage> createState() => _BridgePageState();
}

class _BridgePageState extends State<BridgePage> {
  static const _routes = [
    _RouteSpec(
      id: 0,
      title: '听到设备',
      detail: '从 UAC2 麦克风读，送到本机喇叭。设备 aplay 过来的声音从这里出。',
      capturePrefersUac: true,
    ),
    _RouteSpec(
      id: 1,
      title: '对着设备说',
      detail: '从本机麦克风读，送到 UAC2 扬声器。设备 arecord 能收到这边的声音。',
      capturePrefersUac: false,
    ),
  ];

  final _audio = UacAudio();
  final _capture = <int, int?>{};
  final _playback = <int, int?>{};
  final _running = <int, bool>{};
  final _peak = <int, int>{};
  String _status = '正在读取声卡…';
  Timer? _meter;

  @override
  void initState() {
    super.initState();
    _reload(applyDefaults: true);
  }

  @override
  void dispose() {
    _meter?.cancel();
    for (final route in _routes) {
      if (_running[route.id] == true) {
        _audio.stopRoute(route.id);
      }
    }
    super.dispose();
  }

  Future<void> _reload({required bool applyDefaults}) async {
    final previousCapture = {
      for (final route in _routes) route.id: _nameOf(_audio.captureDevices, _capture[route.id]),
    };
    final previousPlayback = {
      for (final route in _routes) route.id: _nameOf(_audio.playbackDevices, _playback[route.id]),
    };
    final error = _audio.refresh();
    setState(() {
      _status = error.isEmpty ? '已读取 ${_audio.captureDevices.length} 个采集、${_audio.playbackDevices.length} 个播放' : error;
      for (final route in _routes) {
        _capture[route.id] = _keepOrPick(
          devices: _audio.captureDevices,
          currentName: applyDefaults ? null : previousCapture[route.id],
          preferUac: route.capturePrefersUac,
        );
        _playback[route.id] = _keepOrPick(
          devices: _audio.playbackDevices,
          currentName: applyDefaults ? null : previousPlayback[route.id],
          preferUac: !route.capturePrefersUac,
        );
      }
    });
  }

  String? _nameOf(List<AudioDevice> devices, int? index) {
    for (final device in devices) {
      if (device.index == index) {
        return device.name;
      }
    }
    return null;
  }

  int? _keepOrPick({
    required List<AudioDevice> devices,
    required String? currentName,
    required bool preferUac,
  }) {
    if (currentName != null) {
      for (final device in devices) {
        if (device.name == currentName) {
          return device.index;
        }
      }
    }
    for (final device in devices) {
      if (device.isUac == preferUac) {
        return device.index;
      }
    }
    if (devices.isEmpty) {
      return null;
    }
    return devices.first.index;
  }

  Future<bool> _ensureMicrophone() async {
    if (!Platform.isAndroid && !Platform.isMacOS && !Platform.isWindows) {
      return true;
    }
    final status = await Permission.microphone.request();
    return status.isGranted;
  }

  Future<void> _toggle(int routeId) async {
    if (_running[routeId] == true) {
      _audio.stopRoute(routeId);
      setState(() => _running[routeId] = false);
      _stopMeterIfIdle();
      return;
    }

    final capture = _capture[routeId];
    final playback = _playback[routeId];
    if (capture == null || playback == null) {
      setState(() => _status = '这一路还没有可用的采集或播放设备');
      return;
    }
    if (!await _ensureMicrophone()) {
      setState(() => _status = '没有麦克风权限');
      return;
    }

    final error = _audio.startRoute(routeId: routeId, captureIndex: capture, playbackIndex: playback);
    setState(() {
      if (error.isEmpty) {
        _running[routeId] = true;
        _status = '环路已打开';
      } else {
        _running[routeId] = false;
        _status = error;
      }
    });
    if (error.isEmpty) {
      _meter ??= Timer.periodic(const Duration(milliseconds: 200), (_) {
        if (!mounted) {
          return;
        }
        setState(() {
          for (final route in _routes) {
            _peak[route.id] = _running[route.id] == true ? _audio.routePeak(route.id) : 0;
          }
        });
      });
    }
  }

  void _stopMeterIfIdle() {
    final anyRunning = _running.values.any((running) => running);
    if (!anyRunning) {
      _meter?.cancel();
      _meter = null;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('UAC 环路'),
        actions: [
          IconButton(
            onPressed: () => _reload(applyDefaults: false),
            tooltip: '刷新声卡',
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: Align(
        alignment: Alignment.topCenter,
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 760),
          child: ListView(
            padding: const EdgeInsets.all(20),
            children: [
              Text(_status, style: Theme.of(context).textTheme.bodyMedium),
              const SizedBox(height: 16),
              for (final route in _routes) ...[
                _RouteCard(
                  spec: route,
                  captureDevices: _audio.captureDevices,
                  playbackDevices: _audio.playbackDevices,
                  captureIndex: _capture[route.id],
                  playbackIndex: _playback[route.id],
                  running: _running[route.id] == true,
                  peak: _peak[route.id] ?? 0,
                  onCapture: (index) => setState(() => _capture[route.id] = index),
                  onPlayback: (index) => setState(() => _playback[route.id] = index),
                  onToggle: () => _toggle(route.id),
                ),
                const SizedBox(height: 16),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

class _RouteCard extends StatelessWidget {
  const _RouteCard({
    required this.spec,
    required this.captureDevices,
    required this.playbackDevices,
    required this.captureIndex,
    required this.playbackIndex,
    required this.running,
    required this.peak,
    required this.onCapture,
    required this.onPlayback,
    required this.onToggle,
  });

  final _RouteSpec spec;
  final List<AudioDevice> captureDevices;
  final List<AudioDevice> playbackDevices;
  final int? captureIndex;
  final int? playbackIndex;
  final bool running;
  final int peak;
  final ValueChanged<int?> onCapture;
  final ValueChanged<int?> onPlayback;
  final VoidCallback onToggle;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(spec.title, style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 4),
            Text(spec.detail),
            const SizedBox(height: 12),
            _DeviceMenu(
              label: '采集',
              devices: captureDevices,
              value: captureIndex,
              onChanged: running ? null : onCapture,
            ),
            const SizedBox(height: 8),
            _DeviceMenu(
              label: '播放',
              devices: playbackDevices,
              value: playbackIndex,
              onChanged: running ? null : onPlayback,
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                FilledButton(
                  onPressed: onToggle,
                  child: Text(running ? '停止' : '开始'),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: LinearProgressIndicator(
                    value: running ? (peak / 32767).clamp(0, 1).toDouble() : 0,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _DeviceMenu extends StatelessWidget {
  const _DeviceMenu({
    required this.label,
    required this.devices,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final List<AudioDevice> devices;
  final int? value;
  final ValueChanged<int?>? onChanged;

  @override
  Widget build(BuildContext context) {
    return InputDecorator(
      decoration: InputDecoration(
        labelText: label,
        border: const OutlineInputBorder(),
        isDense: true,
      ),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<int>(
          isExpanded: true,
          isDense: true,
          value: devices.any((device) => device.index == value) ? value : null,
          items: [
            for (final device in devices)
              DropdownMenuItem(
                value: device.index,
                child: Text(
                  device.isUac ? '${device.name}  · UAC' : device.name,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
          ],
          onChanged: onChanged,
        ),
      ),
    );
  }
}
