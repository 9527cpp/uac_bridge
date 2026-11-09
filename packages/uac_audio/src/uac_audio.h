#include <stdint.h>

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

/* playback != 0 lists output devices. playback == 0 lists input devices. */
FFI_PLUGIN_EXPORT int uac_refresh(void);
FFI_PLUGIN_EXPORT int uac_device_count(int playback);
FFI_PLUGIN_EXPORT int uac_device_name(int playback, int index, char *out, int out_len);

/* Copies the last error into out. Returns 0 when there is no error. */
FFI_PLUGIN_EXPORT int uac_last_error(char *out, int out_len);

/* Opens one duplex route: capture device -> playback device. route_id is 0..3. */
FFI_PLUGIN_EXPORT int uac_route_start(int route_id, int capture_index, int playback_index);
FFI_PLUGIN_EXPORT void uac_route_stop(int route_id);
FFI_PLUGIN_EXPORT int uac_route_active(int route_id);
/* Peak absolute sample of the capture side, 0..32767. */
FFI_PLUGIN_EXPORT int uac_route_peak(int route_id);
