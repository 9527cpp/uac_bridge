import 'package:flutter_test/flutter_test.dart';
import 'package:uac_audio/uac_audio.dart';

void main() {
  test('refresh returns playback and capture lists', () {
    final audio = UacAudio();
    final error = audio.refresh();
    expect(error, isEmpty);
    expect(audio.playbackDevices, isA<List<AudioDevice>>());
    expect(audio.captureDevices, isA<List<AudioDevice>>());
  });
}
