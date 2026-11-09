// ignore_for_file: non_constant_identifier_names

import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

@ffi.Native<ffi.Int Function()>()
external int uac_refresh();

@ffi.Native<ffi.Int Function(ffi.Int)>()
external int uac_device_count(int playback);

@ffi.Native<ffi.Int Function(ffi.Int, ffi.Int, ffi.Pointer<Utf8>, ffi.Int)>()
external int uac_device_name(int playback, int index, ffi.Pointer<Utf8> out, int outLen);

@ffi.Native<ffi.Int Function(ffi.Pointer<Utf8>, ffi.Int)>()
external int uac_last_error(ffi.Pointer<Utf8> out, int outLen);

@ffi.Native<ffi.Int Function(ffi.Int, ffi.Int, ffi.Int)>()
external int uac_route_start(int routeId, int captureIndex, int playbackIndex);

@ffi.Native<ffi.Void Function(ffi.Int)>()
external void uac_route_stop(int routeId);

@ffi.Native<ffi.Int Function(ffi.Int)>()
external int uac_route_active(int routeId);

@ffi.Native<ffi.Int Function(ffi.Int)>()
external int uac_route_peak(int routeId);
