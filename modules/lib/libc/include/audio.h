#pragma once

#include <stdint.h>

#define AUDIO_DEVICE   "/dev/audio"
#define AUDIO_RATE     48000
#define AUDIO_CHANNELS 2
#define AUDIO_FRAME    (AUDIO_CHANNELS * 2)

#define AUDIO_GET_INFO   0x1100
#define AUDIO_GET_STATE  0x1101
#define AUDIO_SET_VOLUME 0x1102
#define AUDIO_GET_VOLUME 0x1103
#define AUDIO_DROP       0x1104
#define AUDIO_LIST_DEVICES      0x1105
#define AUDIO_SET_OUTPUT_DEVICE 0x1106
#define AUDIO_SET_INPUT_DEVICE  0x1107
#define AUDIO_SET_INPUT_VOLUME  0x1108
#define AUDIO_GET_INPUT_VOLUME  0x1109

#define AUDIO_MAX_DEVICES 16

#define AUDIO_TYPE_LINE_OUT 0x0
#define AUDIO_TYPE_SPEAKER  0x1
#define AUDIO_TYPE_HEADPHONES 0x2
#define AUDIO_TYPE_LINE_IN  0x8
#define AUDIO_TYPE_MIC      0xA

struct audio_info
{
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint8_t  playback;
    uint8_t  capture;
    uint8_t  reserved[2];
    uint32_t period_bytes;
    uint32_t queue_bytes;
    uint32_t latency_bytes;
};

struct audio_state
{
    uint32_t play_queued;
    uint32_t play_free;
    uint32_t capture_avail;
    uint32_t reserved;
};

struct audio_device
{
    uint32_t id;
    uint8_t  input;
    uint8_t  selected;
    uint8_t  type;
    uint8_t  reserved;
    char     name[48];
    char     key[40];
};

struct audio_device_list
{
    uint32_t            count;
    uint32_t            reserved;
    struct audio_device devices[AUDIO_MAX_DEVICES];
};

_Static_assert(sizeof(struct audio_device) == 96, "audio_device must match the kernel's AudioDevice");
_Static_assert(sizeof(struct audio_info) == 24, "audio_info must match the kernel's AudioInfo");
_Static_assert(sizeof(struct audio_state) == 16, "audio_state must match the kernel's AudioState");
