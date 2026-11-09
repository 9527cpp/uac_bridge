import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    final os = input.config.code.targetOS;
    final libraries = <String>[];
    final flags = <String>['-w'];
    final defines = <String, String?>{};

    switch (os) {
      case OS.linux:
        libraries.addAll(['dl', 'pthread', 'm']);
        flags.add('-pthread');
      case OS.android:
        libraries.addAll(['log', 'm', 'aaudio', 'OpenSLES']);
      case OS.windows:
        libraries.addAll(['ole32', 'avrt', 'uuid', 'user32']);
      case OS.macOS || OS.iOS:
        // Sandboxed apps cannot dlopen CoreAudio. Link the frameworks directly.
        defines['MA_NO_RUNTIME_LINKING'] = null;
        flags.addAll([
          '-framework',
          'CoreFoundation',
          '-framework',
          'CoreAudio',
          '-framework',
          'AudioToolbox',
        ]);
      default:
        break;
    }

    final packageName = input.packageName;
    final cbuilder = CBuilder.library(
      name: packageName,
      assetName: '${packageName}_bindings_generated.dart',
      sources: ['src/$packageName.c'],
      libraries: libraries,
      frameworks: const [],
      flags: flags,
      defines: defines,
      optimizationLevel: OptimizationLevel.o2,
    );
    await cbuilder.run(
      input: input,
      output: output,
      logger: Logger('')
        ..level = Level.INFO
        ..onRecord.listen((record) => stderr.writeln(record.message)),
    );
  });
}
