/*
 * 嵌入式设备上的双向环路：
 *   设备麦克风 -> UAC2 播放。主机把它当成 UAC2 麦克风。
 *   UAC2 采集 -> 设备喇叭。主机把它当成 UAC2 扬声器。
 *
 * 内核里 UAC 播放（aplay）是 USB IN，UAC 采集（arecord）是 USB OUT。
 * 外置麦和喇叭用运行参数指定，写成 /dev/snd/pcmC1D0c 或 hw:1,0。
 * 外置设备按 hw 节点打开。采样率或声道和 UAC2 不同时，转成
 * 48000 Hz、双声道、S16_LE。两条方向各用一对单向设备和一个环形缓冲。
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UAC_RATE 48000
#define UAC_CHANNELS 2

static ma_context g_context;
static ma_pcm_rb g_to_host;
static ma_pcm_rb g_from_host;
static ma_device g_mic;
static ma_device g_uac_play;
static ma_device g_uac_cap;
static ma_device g_speaker;
static int g_mic_open;
static int g_uac_play_open;
static int g_uac_cap_open;
static int g_speaker_open;
static volatile sig_atomic_t g_stop;

static void on_signal(int signo)
{
    (void)signo;
    g_stop = 1;
}

typedef struct {
    ma_pcm_rb *rb;
    ma_data_converter converter;
    int convert;
} capture_side;

static void write_rb(ma_pcm_rb *rb, const ma_int16 *samples, ma_uint32 frame_count)
{
    while (frame_count > 0) {
        void *buffer = NULL;
        ma_uint32 chunk = frame_count;

        if (ma_pcm_rb_acquire_write(rb, &chunk, &buffer) != MA_SUCCESS || chunk == 0) {
            break;
        }
        memcpy(buffer, samples, (size_t)chunk * UAC_CHANNELS * sizeof(ma_int16));
        ma_pcm_rb_commit_write(rb, chunk);
        samples += chunk * UAC_CHANNELS;
        frame_count -= chunk;
    }
}

static void capture_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
    capture_side *side = device->pUserData;
    ma_uint32 remaining = frame_count;

    (void)output;
    if (input == NULL || side == NULL) {
        return;
    }
    if (!side->convert) {
        write_rb(side->rb, input, frame_count);
        return;
    }

    while (remaining > 0) {
        ma_int16 converted[2048 * UAC_CHANNELS];
        ma_uint64 in_count = remaining;
        ma_uint64 out_count = 2048;

        if (ma_data_converter_process_pcm_frames(&side->converter, input, &in_count, converted, &out_count) != MA_SUCCESS) {
            break;
        }
        if (in_count == 0 && out_count == 0) {
            break;
        }
        write_rb(side->rb, converted, (ma_uint32)out_count);
        input = (const ma_int16 *)input + in_count * device->capture.channels;
        remaining -= (ma_uint32)in_count;
    }
}

static void playback_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
    ma_pcm_rb *rb = device->pUserData;
    ma_int16 *samples = output;

    (void)input;
    if (output == NULL) {
        return;
    }
    memset(output, 0, (size_t)frame_count * UAC_CHANNELS * sizeof(ma_int16));
    while (frame_count > 0) {
        void *buffer = NULL;
        ma_uint32 chunk = frame_count;

        if (ma_pcm_rb_acquire_read(rb, &chunk, &buffer) != MA_SUCCESS || chunk == 0) {
            break;
        }
        memcpy(samples, buffer, (size_t)chunk * UAC_CHANNELS * sizeof(ma_int16));
        ma_pcm_rb_commit_read(rb, chunk);
        samples += chunk * UAC_CHANNELS;
        frame_count -= chunk;
    }
}

static void usage(void)
{
    fprintf(stderr,
            "用法: uac_device_loop --mic <采集设备> --speaker <播放设备> [--uac <UAC设备>]\n"
            "\n"
            "  --mic       设备麦克风，例如 /dev/snd/pcmC1D0c 或 hw:1,0\n"
            "  --speaker   设备喇叭，例如 /dev/snd/pcmC1D0p 或 hw:1,0\n"
            "  --uac       UAC2 声卡。省略时从 /proc/asound/cards 查找名字里带 UAC2 的卡\n"
            "\n"
            "麦克风经 UAC2 送到主机，主机送来的声音从喇叭播放。\n");
}

static int parse_card_dev(const char *text, int *card, int *dev)
{
    unsigned int card_id;
    unsigned int device_id;
    const char *name = strrchr(text, '/');

    name = name == NULL ? text : name + 1;
    if (sscanf(name, "pcmC%uD%u", &card_id, &device_id) == 2) {
        *card = (int)card_id;
        *dev = (int)device_id;
        return 0;
    }

    if (strncmp(text, "plughw:", 7) == 0) {
        text += 4;
    }
    if (strncmp(text, "hw:", 3) != 0) {
        return -1;
    }
    if (sscanf(text + 3, "%u,%u", &card_id, &device_id) == 2) {
        *card = (int)card_id;
        *dev = (int)device_id;
        return 0;
    }
    return -1;
}

static int alsa_names(const char *text, char *hw, size_t hw_len, char *plug, size_t plug_len)
{
    int card;
    int dev;

    if (parse_card_dev(text, &card, &dev) == 0) {
        snprintf(hw, hw_len, "hw:%d,%d", card, dev);
        snprintf(plug, plug_len, "plughw:%d,%d", card, dev);
        return 0;
    }
    if (strncmp(text, "hw:", 3) == 0) {
        snprintf(hw, hw_len, "%s", text);
        snprintf(plug, plug_len, "plug%s", text);
        return 0;
    }
    if (strncmp(text, "plughw:", 7) == 0) {
        snprintf(plug, plug_len, "%s", text);
        snprintf(hw, hw_len, "%s", text + 4);
        return 0;
    }
    return -1;
}

static int find_uac_card(void)
{
    FILE *cards = fopen("/proc/asound/cards", "r");
    char line[256];
    int found = -1;

    if (cards == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), cards) != NULL) {
        int index;
        char id[64];

        if (sscanf(line, " %d [%63[^]]", &index, id) != 2) {
            continue;
        }
        if (strstr(line, "UAC2") != NULL || strstr(id, "UAC") != NULL) {
            found = index;
            break;
        }
    }
    fclose(cards);
    return found;
}

static capture_side g_mic_side;
static capture_side g_uac_side;

static ma_result open_end(ma_device *device, ma_device_type type, const char *name, void *user, ma_uint32 rate, ma_uint32 channels)
{
    ma_device_id id;
    ma_device_config config;

    memset(&id, 0, sizeof(id));
    snprintf(id.alsa, sizeof(id.alsa), "%s", name);
    config = ma_device_config_init(type);
    config.sampleRate = rate;
    config.periodSizeInFrames = rate == 0 ? 0 : rate / 100;
    config.capture.format = ma_format_s16;
    config.playback.format = ma_format_s16;
    config.capture.channels = type == ma_device_type_capture ? channels : UAC_CHANNELS;
    config.playback.channels = type == ma_device_type_playback ? channels : UAC_CHANNELS;
    config.pUserData = user;
    config.dataCallback = type == ma_device_type_capture ? capture_callback : playback_callback;
    if (type == ma_device_type_capture) {
        config.capture.pDeviceID = &id;
    } else {
        config.playback.pDeviceID = &id;
    }
    return ma_device_init(&g_context, &config, device);
}

static ma_result prepare_capture(capture_side *side, ma_device *device, ma_pcm_rb *rb)
{
    ma_channel stereo[UAC_CHANNELS] = {MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT};
    ma_data_converter_config config;

    side->rb = rb;
    side->convert = 0;
    if (device->sampleRate == UAC_RATE && device->capture.channels == UAC_CHANNELS && device->capture.format == ma_format_s16) {
        return MA_SUCCESS;
    }

    config = ma_data_converter_config_init(device->capture.format, ma_format_s16, device->capture.channels, UAC_CHANNELS, device->sampleRate, UAC_RATE);
    config.pChannelMapIn = device->capture.channelMap;
    config.pChannelMapOut = stereo;
    if (ma_data_converter_init(&config, NULL, &side->converter) != MA_SUCCESS) {
        return MA_ERROR;
    }
    side->convert = 1;
    return MA_SUCCESS;
}

static void close_opened(void)
{
    if (g_mic_side.convert) {
        ma_data_converter_uninit(&g_mic_side.converter, NULL);
        g_mic_side.convert = 0;
    }
    if (g_uac_side.convert) {
        ma_data_converter_uninit(&g_uac_side.converter, NULL);
        g_uac_side.convert = 0;
    }
    if (g_speaker_open) {
        ma_device_uninit(&g_speaker);
    }
    if (g_uac_cap_open) {
        ma_device_uninit(&g_uac_cap);
    }
    if (g_uac_play_open) {
        ma_device_uninit(&g_uac_play);
    }
    if (g_mic_open) {
        ma_device_uninit(&g_mic);
    }
    ma_pcm_rb_uninit(&g_to_host);
    ma_pcm_rb_uninit(&g_from_host);
    ma_context_uninit(&g_context);
}

int main(int argc, char **argv)
{
    const char *mic_arg = NULL;
    const char *speaker_arg = NULL;
    const char *uac_arg = NULL;
    char mic_hw[64];
    char mic_plug[64];
    char speaker_hw[64];
    char speaker_plug[64];
    char uac_hw[64];
    char uac_plug[64];
    ma_backend backend = ma_backend_alsa;
    ma_result result;
    int i;

    for (i = 1; i < argc; i++) {
        const char *value = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(argv[i], "--mic") == 0 && value != NULL) {
            mic_arg = value;
            i++;
        } else if (strcmp(argv[i], "--speaker") == 0 && value != NULL) {
            speaker_arg = value;
            i++;
        } else if (strcmp(argv[i], "--uac") == 0 && value != NULL) {
            uac_arg = value;
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "无法识别的参数: %s\n", argv[i]);
            usage();
            return 1;
        }
    }

    if (mic_arg == NULL || speaker_arg == NULL) {
        usage();
        return 1;
    }
    if (alsa_names(mic_arg, mic_hw, sizeof(mic_hw), mic_plug, sizeof(mic_plug)) != 0) {
        fprintf(stderr, "麦克风路径无法识别: %s\n", mic_arg);
        return 1;
    }
    if (alsa_names(speaker_arg, speaker_hw, sizeof(speaker_hw), speaker_plug, sizeof(speaker_plug)) != 0) {
        fprintf(stderr, "喇叭路径无法识别: %s\n", speaker_arg);
        return 1;
    }
    if (uac_arg != NULL) {
        if (alsa_names(uac_arg, uac_hw, sizeof(uac_hw), uac_plug, sizeof(uac_plug)) != 0) {
            fprintf(stderr, "UAC 路径无法识别: %s\n", uac_arg);
            return 1;
        }
    } else {
        int card = find_uac_card();

        if (card < 0) {
            fprintf(stderr, "没有找到 UAC2 声卡，请用 --uac 指定\n");
            return 1;
        }
        snprintf(uac_hw, sizeof(uac_hw), "hw:%d,0", card);
    }

    (void)mic_plug;
    (void)speaker_plug;
    (void)uac_plug;

    result = ma_context_init(&backend, 1, NULL, &g_context);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "初始化 ALSA 失败: %s\n", ma_result_description(result));
        return 1;
    }
    result = ma_pcm_rb_init(ma_format_s16, UAC_CHANNELS, UAC_RATE, NULL, NULL, &g_to_host);
    if (result == MA_SUCCESS) {
        result = ma_pcm_rb_init(ma_format_s16, UAC_CHANNELS, UAC_RATE, NULL, NULL, &g_from_host);
    }
    if (result != MA_SUCCESS) {
        fprintf(stderr, "创建音频缓冲失败: %s\n", ma_result_description(result));
        ma_context_uninit(&g_context);
        return 1;
    }

    result = open_end(&g_mic, ma_device_type_capture, mic_hw, &g_mic_side, 0, 0);
    g_mic_open = result == MA_SUCCESS;
    if (result == MA_SUCCESS) {
        result = prepare_capture(&g_mic_side, &g_mic, &g_to_host);
    }
    if (result != MA_SUCCESS) {
        fprintf(stderr, "打开麦克风 %s 失败: %s\n", mic_hw, ma_result_description(result));
        close_opened();
        return 1;
    }
    result = open_end(&g_uac_play, ma_device_type_playback, uac_hw, &g_to_host, UAC_RATE, UAC_CHANNELS);
    g_uac_play_open = result == MA_SUCCESS;
    if (result != MA_SUCCESS) {
        fprintf(stderr, "打开 UAC2 播放 %s 失败: %s\n", uac_hw, ma_result_description(result));
        close_opened();
        return 1;
    }
    result = open_end(&g_uac_cap, ma_device_type_capture, uac_hw, &g_uac_side, UAC_RATE, UAC_CHANNELS);
    g_uac_cap_open = result == MA_SUCCESS;
    if (result == MA_SUCCESS) {
        result = prepare_capture(&g_uac_side, &g_uac_cap, &g_from_host);
    }
    if (result != MA_SUCCESS) {
        fprintf(stderr, "打开 UAC2 采集 %s 失败: %s\n", uac_hw, ma_result_description(result));
        close_opened();
        return 1;
    }
    result = open_end(&g_speaker, ma_device_type_playback, speaker_hw, &g_from_host, UAC_RATE, UAC_CHANNELS);
    g_speaker_open = result == MA_SUCCESS;
    if (result != MA_SUCCESS) {
        fprintf(stderr, "打开喇叭 %s 失败: %s\n", speaker_hw, ma_result_description(result));
        close_opened();
        return 1;
    }

    result = ma_device_start(&g_mic);
    if (result == MA_SUCCESS) {
        result = ma_device_start(&g_uac_play);
    }
    if (result == MA_SUCCESS) {
        result = ma_device_start(&g_uac_cap);
    }
    if (result == MA_SUCCESS) {
        result = ma_device_start(&g_speaker);
    }
    if (result != MA_SUCCESS) {
        fprintf(stderr, "启动环路失败: %s\n", ma_result_description(result));
        close_opened();
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("麦克风 %s (%u Hz, %u 声道) -> UAC2 %s\n", mic_arg, g_mic.sampleRate, g_mic.capture.channels, uac_hw);
    printf("UAC2 %s -> 喇叭 %s\n", uac_hw, speaker_arg);
    fflush(stdout);

    while (!g_stop) {
        pause();
    }

    ma_device_stop(&g_mic);
    ma_device_stop(&g_uac_play);
    ma_device_stop(&g_uac_cap);
    ma_device_stop(&g_speaker);
    close_opened();
    return 0;
}
