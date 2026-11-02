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

_Static_assert(sizeof(struct audio_info) == 24, "audio_info must match the kernel's AudioInfo");
_Static_assert(sizeof(struct audio_state) == 16, "audio_state must match the kernel's AudioState");
