#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <ioctl.h>
#include <audio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int usage(void)
{
    fprintf(stderr,
        "usage: audio info\n"
        "       audio tone [hz] [seconds]\n"
        "       audio play <file.wav>\n"
        "       audio record <file.wav> [seconds]\n"
        "       audio volume [0-100]\n");
    return 2;
}

static int open_audio(int flags)
{
    int fd = open(AUDIO_DEVICE, flags);
    if (fd < 0)
        fprintf(stderr, "audio: cannot open %s (no sound card?)\n", AUDIO_DEVICE);
    return fd;
}

static bool write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len)
    {
        ssize_t n = write(fd, p, len);
        if (n <= 0)
            return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static int cmd_info(void)
{
    int fd = open_audio(O_WRONLY);
    if (fd < 0)
        return 1;
    struct audio_info info;
    uint32_t vol = 0;
    if (ioctl(fd, AUDIO_GET_INFO, &info) != 0)
    {
        close(fd);
        return 1;
    }
    ioctl(fd, AUDIO_GET_VOLUME, &vol);
    printf("%u Hz, %u channels, %u bits\n", info.sample_rate, info.channels, info.bits);
    printf("playback: %s, capture: %s\n", info.playback ? "yes" : "no", info.capture ? "yes" : "no");
    printf("period %u bytes, queue %u bytes, mixer lead %u bytes\n",
           info.period_bytes, info.queue_bytes, info.latency_bytes);
    printf("volume %u%%\n", vol);
    close(fd);
    return 0;
}

static int cmd_tone(double hz, double seconds)
{
    int fd = open_audio(O_WRONLY);
    if (fd < 0)
        return 1;

    int16_t buf[480 * 2];
    long frames = (long)(seconds * AUDIO_RATE);
    double phase = 0, step = 2.0 * M_PI * hz / AUDIO_RATE;

    for (long done = 0; done < frames;)
    {
        int n = 0;
        for (; n < 480 && done < frames; n++, done++)
        {
            double env = 1.0;
            long edge = MIN(done, frames - 1 - done);
            if (edge < 480)
                env = (double)edge / 480.0;
            int16_t s = (int16_t)(sin(phase) * 8000.0 * env);
            buf[n * 2] = buf[n * 2 + 1] = s;
            phase += step;
            if (phase > 2.0 * M_PI)
                phase -= 2.0 * M_PI;
        }
        if (!write_all(fd, buf, (size_t)n * AUDIO_FRAME))
            break;
    }
    close(fd);
    return 0;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static int cmd_play(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "audio: cannot open %s\n", path);
        return 1;
    }

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
    {
        fprintf(stderr, "audio: %s is not a WAV file\n", path);
        fclose(f);
        return 1;
    }

    uint16_t channels = 0, bits = 0;
    uint32_t rate = 0, data_len = 0;
    bool have_data = false;
    uint8_t chunk[8];
    while (fread(chunk, 1, 8, f) == 8)
    {
        uint32_t len = le32(chunk + 4);
        if (!memcmp(chunk, "fmt ", 4))
        {
            uint8_t fmt[16];
            if (len < 16 || fread(fmt, 1, 16, f) != 16)
                break;
            if (le16(fmt) != 1)
            {
                fprintf(stderr, "audio: only PCM WAV files are supported\n");
                fclose(f);
                return 1;
            }
            channels = le16(fmt + 2);
            rate = le32(fmt + 4);
            bits = le16(fmt + 14);
            fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        }
        else if (!memcmp(chunk, "data", 4))
        {
            data_len = len;
            have_data = true;
            break;
        }
        else
        {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }

    if (!have_data || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate == 0)
    {
        fprintf(stderr, "audio: unsupported WAV (need 8/16-bit mono/stereo PCM)\n");
        fclose(f);
        return 1;
    }

    int fd = open_audio(O_WRONLY);
    if (fd < 0)
    {
        fclose(f);
        return 1;
    }

    printf("playing %s: %u Hz, %u ch, %u bits\n", path, rate, channels, bits);

    size_t in_frame = channels * (bits / 8);
    uint8_t in[4096];
    int16_t out[2048 * 2];
    uint64_t pos = 0;
    uint64_t consumed = 0;
    size_t in_have = 0;
    size_t remaining = data_len;
    bool eof = false;

    for (;;)
    {
        if (!eof && in_have < sizeof(in) / 2)
        {
            size_t want = MIN(sizeof(in) - in_have, remaining);
            size_t n = want ? fread(in + in_have, 1, want, f) : 0;
            in_have += n;
            remaining -= n;
            if (n == 0)
                eof = true;
        }

        int n = 0;
        for (; n < 2048; n++)
        {
            uint64_t src = pos * rate / AUDIO_RATE;
            size_t idx = (size_t)(src - consumed);
            if ((idx + 1) * in_frame > in_have)
                break;
            const uint8_t *p = in + idx * in_frame;
            int16_t l, r;
            if (bits == 16)
            {
                l = (int16_t)le16(p);
                r = channels == 2 ? (int16_t)le16(p + 2) : l;
            }
            else
            {
                l = (int16_t)((p[0] - 128) << 8);
                r = channels == 2 ? (int16_t)((p[1] - 128) << 8) : l;
            }
            out[n * 2] = l;
            out[n * 2 + 1] = r;
            pos++;
        }

        if (n == 0 && eof)
            break;
        if (n > 0 && !write_all(fd, out, (size_t)n * AUDIO_FRAME))
            break;

        uint64_t next = pos * rate / AUDIO_RATE;
        size_t drop = (size_t)MIN(next - consumed, (uint64_t)(in_have / in_frame));
        memmove(in, in + drop * in_frame, in_have - drop * in_frame);
        in_have -= drop * in_frame;
        consumed += drop;
    }

    fclose(f);
    close(fd);
    return 0;
}

static int cmd_record(const char *path, double seconds)
{
    int fd = open_audio(O_RDONLY);
    if (fd < 0)
        return 1;

    FILE *f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "audio: cannot create %s\n", path);
        close(fd);
        return 1;
    }

    uint32_t total = (uint32_t)(seconds * AUDIO_RATE) * AUDIO_FRAME;
    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4);
    put32(hdr + 4, 36 + total);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put32(hdr + 16, 16);
    put16(hdr + 20, 1);
    put16(hdr + 22, AUDIO_CHANNELS);
    put32(hdr + 24, AUDIO_RATE);
    put32(hdr + 28, AUDIO_RATE * AUDIO_FRAME);
    put16(hdr + 32, AUDIO_FRAME);
    put16(hdr + 34, 16);
    memcpy(hdr + 36, "data", 4);
    put32(hdr + 40, total);
    fwrite(hdr, 1, sizeof(hdr), f);

    printf("recording %.1f s to %s\n", seconds, path);

    uint8_t buf[3840];
    uint32_t done = 0;
    int32_t peak = 0;
    while (done < total)
    {
        size_t want = MIN(sizeof(buf), (size_t)(total - done));
        ssize_t n = read(fd, buf, want);
        if (n <= 0)
            break;
        for (ssize_t i = 0; i + 1 < n; i += 2)
        {
            int32_t s = (int16_t)le16(buf + i);
            if (s < 0)
                s = -s;
            if (s > peak)
                peak = s;
        }
        fwrite(buf, 1, (size_t)n, f);
        done += (uint32_t)n;
    }

    fclose(f);
    close(fd);
    printf("recorded %u bytes, peak level %d%%\n", done, (int)(peak * 100 / 32768));
    return 0;
}

static int cmd_volume(int argc, char **argv)
{
    int fd = open_audio(O_WRONLY);
    if (fd < 0)
        return 1;
    uint32_t vol;
    if (argc > 2)
    {
        vol = (uint32_t)atoi(argv[2]);
        ioctl(fd, AUDIO_SET_VOLUME, &vol);
    }
    ioctl(fd, AUDIO_GET_VOLUME, &vol);
    printf("volume %u%%\n", vol);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();

    const char *cmd = argv[1];
    if (!strcmp(cmd, "info"))
        return cmd_info();
    if (!strcmp(cmd, "tone"))
        return cmd_tone(argc > 2 ? atof(argv[2]) : 440.0, argc > 3 ? atof(argv[3]) : 1.0);
    if (!strcmp(cmd, "play") && argc > 2)
        return cmd_play(argv[2]);
    if (!strcmp(cmd, "record") && argc > 2)
        return cmd_record(argv[2], argc > 3 ? atof(argv[3]) : 5.0);
    if (!strcmp(cmd, "volume"))
        return cmd_volume(argc, argv);
    return usage();
}
