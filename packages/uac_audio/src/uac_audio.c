#include "uac_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define UAC_MAX_DEVICES 64
#define UAC_MAX_ROUTES 4
#define UAC_NAME_CAP 256

typedef struct {
    ma_device_id id;
    char name[UAC_NAME_CAP];
} uac_device_entry;

static ma_context g_context;
static int g_context_ready;
static uac_device_entry g_playback[UAC_MAX_DEVICES];
static uac_device_entry g_capture[UAC_MAX_DEVICES];
static int g_playback_count;
static int g_capture_count;

static ma_device g_routes[UAC_MAX_ROUTES];
static int g_route_on[UAC_MAX_ROUTES];
static volatile int g_peak[UAC_MAX_ROUTES];
static char g_error[512];

static void set_error(const char *message) {
    snprintf(g_error, sizeof(g_error), "%s", message);
}

static void copy_devices(const ma_device_info *infos, ma_uint32 count, uac_device_entry *dest, int *dest_count) {
    int n = (int)count;
    if (n > UAC_MAX_DEVICES) {
        n = UAC_MAX_DEVICES;
    }
    for (int i = 0; i < n; i++) {
        dest[i].id = infos[i].id;
        snprintf(dest[i].name, UAC_NAME_CAP, "%s", infos[i].name);
    }
    *dest_count = n;
}

static void route_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count) {
    int route_id = (int)(intptr_t)device->pUserData;
    ma_uint32 channels = device->playback.channels;
    size_t bytes = (size_t)frame_count * channels * sizeof(int16_t);

    if (output == NULL) {
        return;
    }
    if (input == NULL) {
        memset(output, 0, bytes);
        if (route_id >= 0 && route_id < UAC_MAX_ROUTES) {
            g_peak[route_id] = 0;
        }
        return;
    }

    memcpy(output, input, bytes);

    if (route_id >= 0 && route_id < UAC_MAX_ROUTES) {
        const int16_t *samples = (const int16_t *)input;
        ma_uint32 total = frame_count * channels;
        int peak = 0;
        for (ma_uint32 i = 0; i < total; i += 16) {
            int value = samples[i];
            if (value < 0) {
                value = -value;
            }
            if (value > peak) {
                peak = value;
            }
        }
        g_peak[route_id] = peak;
    }
}

static int try_open(int route_id, const ma_device_id *capture, const ma_device_id *playback, ma_uint32 rate, ma_uint32 channels) {
    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.sampleRate = rate;
    config.periodSizeInFrames = rate / 100;
    config.capture.format = ma_format_s16;
    config.capture.channels = channels;
    config.capture.pDeviceID = capture;
    config.playback.format = ma_format_s16;
    config.playback.channels = channels;
    config.playback.pDeviceID = playback;
    config.dataCallback = route_callback;
    config.pUserData = (void *)(intptr_t)route_id;
    return ma_device_init(&g_context, &config, &g_routes[route_id]);
}

FFI_PLUGIN_EXPORT int uac_refresh(void) {
    g_error[0] = '\0';
    if (!g_context_ready) {
        ma_result result = ma_context_init(NULL, 0, NULL, &g_context);
        if (result != MA_SUCCESS) {
            set_error("初始化音频系统失败");
            return -1;
        }
        g_context_ready = 1;
    }

    ma_device_info *playback = NULL;
    ma_device_info *capture = NULL;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    ma_result result = ma_context_get_devices(&g_context, &playback, &playback_count, &capture, &capture_count);
    if (result != MA_SUCCESS) {
        set_error("枚举声卡失败");
        return -1;
    }
    copy_devices(playback, playback_count, g_playback, &g_playback_count);
    copy_devices(capture, capture_count, g_capture, &g_capture_count);
    return 0;
}

FFI_PLUGIN_EXPORT int uac_device_count(int playback) {
    return playback ? g_playback_count : g_capture_count;
}

FFI_PLUGIN_EXPORT int uac_device_name(int playback, int index, char *out, int out_len) {
    if (out == NULL || out_len <= 0) {
        return -1;
    }
    out[0] = '\0';
    const uac_device_entry *table = playback ? g_playback : g_capture;
    int count = playback ? g_playback_count : g_capture_count;
    if (index < 0 || index >= count) {
        return -1;
    }
    snprintf(out, (size_t)out_len, "%s", table[index].name);
    return 0;
}

FFI_PLUGIN_EXPORT int uac_last_error(char *out, int out_len) {
    if (out == NULL || out_len <= 0) {
        return -1;
    }
    snprintf(out, (size_t)out_len, "%s", g_error);
    return g_error[0] == '\0' ? 0 : -1;
}

FFI_PLUGIN_EXPORT int uac_route_start(int route_id, int capture_index, int playback_index) {
    g_error[0] = '\0';
    if (route_id < 0 || route_id >= UAC_MAX_ROUTES) {
        set_error("环路编号无效");
        return -1;
    }
    if (capture_index < 0 || capture_index >= g_capture_count || playback_index < 0 || playback_index >= g_playback_count) {
        set_error("声卡序号无效，先刷新设备列表");
        return -1;
    }
    if (g_route_on[route_id]) {
        uac_route_stop(route_id);
    }

    const ma_uint32 rates[] = {48000, 44100};
    const ma_uint32 channels[] = {2, 1};
    ma_result result = MA_ERROR;
    for (int r = 0; r < 2 && result != MA_SUCCESS; r++) {
        for (int c = 0; c < 2 && result != MA_SUCCESS; c++) {
            result = try_open(route_id, &g_capture[capture_index].id, &g_playback[playback_index].id, rates[r], channels[c]);
        }
    }
    if (result != MA_SUCCESS) {
        set_error("打开采集或播放设备失败");
        return -1;
    }

    result = ma_device_start(&g_routes[route_id]);
    if (result != MA_SUCCESS) {
        ma_device_uninit(&g_routes[route_id]);
        set_error("启动音频环路失败");
        return -1;
    }
    g_route_on[route_id] = 1;
    g_peak[route_id] = 0;
    return 0;
}

FFI_PLUGIN_EXPORT void uac_route_stop(int route_id) {
    if (route_id < 0 || route_id >= UAC_MAX_ROUTES || !g_route_on[route_id]) {
        return;
    }
    ma_device_stop(&g_routes[route_id]);
    ma_device_uninit(&g_routes[route_id]);
    g_route_on[route_id] = 0;
    g_peak[route_id] = 0;
}

FFI_PLUGIN_EXPORT int uac_route_active(int route_id) {
    if (route_id < 0 || route_id >= UAC_MAX_ROUTES) {
        return 0;
    }
    return g_route_on[route_id];
}

FFI_PLUGIN_EXPORT int uac_route_peak(int route_id) {
    if (route_id < 0 || route_id >= UAC_MAX_ROUTES) {
        return 0;
    }
    return g_peak[route_id];
}
