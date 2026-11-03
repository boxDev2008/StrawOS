#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ioctl.h>
#include <socket.h>
#include <audio.h>
#include <opus.h>

#include "../discord.h"
#include "dave.h"
#include "mls.h"
#include "screen.h"
#include "vp8dec.h"
#include "watch.h"
#include <strawx.h>

#define FRAME_SAMPLES   960
#define FRAME_BYTES     (FRAME_SAMPLES * AUDIO_FRAME)
#define MAX_SPEAKERS    32
#define MAX_PACKET      1500
#define VAD_LEVEL       600
#define VAD_HANGOVER_MS 400
#define TALK_TIMEOUT_MS 250
#define PLAY_MAX_QUEUE  (FRAME_BYTES * 8)
#define SILENCE_FRAMES  5
#define DISCOVERY_MS    1000
#define CONNECT_MS      10000
#define MODE            "aead_aes256_gcm_rtpsize"

#define VIDEO_PT        105
#define VIDEO_RTX_PT    106
#define VIDEO_MAX_W     1920
#define VIDEO_MAX_H     1080
#define VIDEO_FPS       15
#define VIDEO_BITRATE   2500000
#define VIDEO_CHUNK     1100
#define VIDEO_QUEUE     768
#define VIDEO_MAX_FRAME (VIDEO_QUEUE * VIDEO_CHUNK)
#define KEY_MIN_MS      1000
#define KEY_EVERY_MS    20000
#define IDLE_FRAME_MS   1000
#define SR_EVERY_MS     1000
#define Q_START         30
#define Q_MIN           12
#define Q_MAX           100
#define RX_MAX_FRAME    (4 << 20)
#define PLI_MIN_MS      300

typedef struct
{
    uint32_t     ssrc;
    uint64_t     user;
    OpusDecoder *dec;
    int          fd;
    uint16_t     last_seq;
    bool         have_seq;
    uint64_t     last_packet;
    bool         talking;
}
Speaker;

static struct
{
    char     endpoint[256];
    char     server[32];
    char     session[128];
    char     token[192];
    uint64_t user_id;
    uint64_t channel_id;

    int      ctl;
    char     ctl_in[512];
    size_t   ctl_len;

    Ws      *ws;
    uint64_t connect_at;
    uint64_t reconnect_at;
    bool     identified;
    bool     resuming;
    int      resumes;
    uint64_t hb_interval;
    uint64_t next_hb;
    int64_t  seq_ack;

    int      udp;
    uint32_t ssrc;
    char     ip[64];
    int      port;
    bool     discovered;
    int      discovery_tries;
    uint64_t discovery_at;

    uint8_t  key[32];
    bool     have_key;
    uint32_t tx_nonce;
    uint16_t rtp_seq;
    uint32_t rtp_ts;

    Dave        *dave;
    uint64_t     shown_epoch;
    OpusEncoder *enc;
    int          mic;
    bool         muted;
    bool         deafened;
    bool         speaking;
    uint64_t     last_voice;
    int          silence_left;

    Speaker  speakers[MAX_SPEAKERS];
    int      n_speakers;

    bool     stream;
    bool     video_on;
    uint32_t video_ssrc;
    uint32_t rtx_ssrc;
    Screen  *screen;
    uint16_t vseq;
    uint32_t vts_base;
    uint32_t vts;
    uint16_t picture_id;
    uint64_t next_frame;
    uint64_t last_frame;
    uint64_t last_key;
    bool     want_key;
    int      q;
    int64_t  credit;
    uint64_t credit_at;
    int64_t  pace;
    uint64_t pace_at;
    uint8_t *queue;
    uint16_t qlen[VIDEO_QUEUE];
    int      q_head, q_count;
    uint32_t v_packets;
    uint32_t v_octets;
    uint64_t next_sr;
    uint8_t *frame_buf;
    uint8_t *dave_buf;

    uint64_t watch_user;
    char     watch_title[128];
    Watch   *win;
    Vp8Dec  *dec;
    uint32_t vssrc[8];
    int      n_vssrc;
    uint8_t *rx_buf;
    size_t   rx_len;
    uint32_t rx_ts;
    uint16_t rx_next;
    bool     rx_active;
    bool     have_vseq;
    uint16_t last_vseq;
    bool     need_key;
    uint64_t last_pli;
    uint8_t *dec_buf;
    bool     shown_video;
    uint64_t watch_at;
    uint32_t video_packets;
    uint32_t foreign_packets;
    int      foreign_pt;
    bool     watch_notice;
}
V;

static void ctl_send(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>

static void ctl_send(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    if (V.ctl >= 0)
        write(V.ctl, line, (size_t)n);
}

static void quit(const char *why)
{
    if (why)
    {
        printf("discord-voice: %s\n", why);
        ctl_send("error %s", why);
    }
    exit(0);
}

static void dave_log(void *user, const char *msg)
{
    (void)user;
    printf("discord-voice: %s\n", msg);
}

static void dave_json(void *user, const char *json)
{
    (void)user;
    if (V.ws)
        ws_send(V.ws, json, strlen(json));
}

static void dave_bin(void *user, int op, const uint8_t *data, size_t len)
{
    (void)user;
    uint8_t *b = malloc(len + 1);
    if (!b || !V.ws)
    {
        free(b);
        return;
    }
    b[0] = (uint8_t)op;
    memcpy(b + 1, data, len);
    ws_send_binary(V.ws, b, len + 1);
    free(b);
}

static void send_json(const char *json)
{
    if (V.ws)
        ws_send(V.ws, json, strlen(json));
}

static void send_identify(void)
{
    Buf b = { 0 };
    if (V.resuming)
    {
        buf_puts(&b, "{\"op\":7,\"d\":{\"server_id\":");
        buf_json_string(&b, V.server);
        buf_puts(&b, ",\"session_id\":");
        buf_json_string(&b, V.session);
        buf_puts(&b, ",\"token\":");
        buf_json_string(&b, V.token);
        buf_printf(&b, ",\"seq_ack\":%lld}}", (long long)V.seq_ack);
    }
    else
    {
        buf_puts(&b, "{\"op\":0,\"d\":{\"server_id\":");
        buf_json_string(&b, V.server);
        buf_printf(&b, ",\"user_id\":\"%llu\",\"session_id\":", (unsigned long long)V.user_id);
        buf_json_string(&b, V.session);
        buf_puts(&b, ",\"token\":");
        buf_json_string(&b, V.token);
        if (V.stream || V.watch_user)
            buf_puts(&b, ",\"video\":true,\"streams\":[{\"type\":\"screen\",\"rid\":\"100\",\"quality\":100}]");
        buf_printf(&b, ",\"max_dave_protocol_version\":%d}}", DAVE_MAX_PROTOCOL);
    }
    ws_send(V.ws, b.data, b.len);
    buf_free(&b);
    V.identified = true;
}

static void connect_ws(void)
{
    char url[320];
    snprintf(url, sizeof(url), "wss://%s/?v=8", V.endpoint);
    ws_free(V.ws);
    V.ws = ws_open(url, NULL);
    V.connect_at = uptime_ms();
    V.reconnect_at = 0;
    V.identified = false;
    V.hb_interval = 0;
}

static void send_speaking(bool on)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"op\":5,\"d\":{\"speaking\":%d,\"delay\":0,\"ssrc\":%u}}", on ? 1 : 0, V.ssrc);
    send_json(json);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void send_rtp(const uint8_t *frame, size_t len)
{
    if (!V.have_key || V.udp < 0)
        return;
    uint8_t enc[MAX_PACKET];
    size_t enc_len;
    if (len + DAVE_FRAME_OVERHEAD > sizeof(enc) || !dave_encrypt(V.dave, frame, len, enc, &enc_len))
        return;
    if (12 + enc_len + 20 > MAX_PACKET)
        return;

    uint8_t pkt[MAX_PACKET];
    pkt[0] = 0x80;
    pkt[1] = 0x78;
    pkt[2] = (uint8_t)(V.rtp_seq >> 8);
    pkt[3] = (uint8_t)V.rtp_seq;
    put32(pkt + 4, V.rtp_ts);
    put32(pkt + 8, V.ssrc);
    V.rtp_seq++;

    uint32_t nonce = V.tx_nonce++;
    uint8_t iv[12] = { 0 }, tag[16];
    put32(iv, nonce);
    memcpy(pkt + 12, enc, enc_len);
    mls_aes_gcm(true, V.key, 32, iv, 12, pkt, 12, pkt + 12, enc_len, tag);
    memcpy(pkt + 12 + enc_len, tag, 16);
    put32(pkt + 12 + enc_len + 16, nonce);
    write(V.udp, pkt, 12 + enc_len + 20);
}

static void request_key(void)
{
    V.want_key = true;
}

static void handle_rtcp(uint8_t *p, size_t n)
{
    if (n < 8 + 20)
        return;
    size_t ct_len = n - 8 - 20;
    uint8_t iv[12] = { 0 };
    memcpy(iv, p + n - 4, 4);
    if (!mls_aes_gcm_check(V.key, 32, iv, 12, p, 8, p + 8, ct_len, p + 8 + ct_len, 16))
        return;
    size_t end = 8 + ct_len, off = 0;
    while (off + 4 <= end)
    {
        int fmt = p[off] & 0x1F, pt = p[off + 1];
        size_t len = ((size_t)(p[off + 2] << 8 | p[off + 3]) + 1) * 4;
        if ((p[off] >> 6) != 2 || off + len > end)
            break;
        if (pt == 206 && (fmt == 1 || fmt == 4))
            request_key();
        off += len;
    }
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void queue_video_packet(const uint8_t *chunk, size_t len, bool first, bool last)
{
    if (V.q_count == VIDEO_QUEUE)
        return;
    uint8_t *pkt = V.queue + (size_t)((V.q_head + V.q_count) % VIDEO_QUEUE) * MAX_PACKET;
    pkt[0] = 0x90;
    pkt[1] = (uint8_t)(VIDEO_PT | (last ? 0x80 : 0));
    put16(pkt + 2, V.vseq++);
    put32(pkt + 4, V.vts);
    put32(pkt + 8, V.video_ssrc);
    pkt[12] = 0xBE;
    pkt[13] = 0xDE;
    pkt[14] = 0;
    pkt[15] = 1;

    uint8_t *body = pkt + 16;
    body[0] = 0x51;
    body[1] = body[2] = body[3] = 0;
    body[4] = (uint8_t)(0x80 | (first ? 0x10 : 0));
    body[5] = 0x80;
    body[6] = (uint8_t)(0x80 | ((V.picture_id >> 8) & 0x7F));
    body[7] = (uint8_t)V.picture_id;
    memcpy(body + 8, chunk, len);
    size_t body_len = 8 + len;

    uint32_t nonce = V.tx_nonce++;
    uint8_t iv[12] = { 0 }, tag[16];
    put32(iv, nonce);
    mls_aes_gcm(true, V.key, 32, iv, 12, pkt, 16, body, body_len, tag);
    memcpy(body + body_len, tag, 16);
    put32(body + body_len + 16, nonce);
    V.qlen[(V.q_head + V.q_count) % VIDEO_QUEUE] = (uint16_t)(16 + body_len + 20);
    V.q_count++;
    V.v_packets++;
    V.v_octets += (uint32_t)(8 + len);
}

static bool send_video_frame(const uint8_t *frame, size_t len, bool key)
{
    size_t enc_len;
    if (!dave_encrypt_video(V.dave, frame, len, key ? 10 : 1, V.dave_buf, &enc_len))
        return false;
    size_t chunks = (enc_len + VIDEO_CHUNK - 1) / VIDEO_CHUNK;
    if (chunks > (size_t)(VIDEO_QUEUE - V.q_count))
        return false;
    V.vts = V.vts_base + (uint32_t)(uptime_ms() * 90);
    for (size_t i = 0, off = 0; i < chunks; i++)
    {
        size_t n = enc_len - off < VIDEO_CHUNK ? enc_len - off : VIDEO_CHUNK;
        queue_video_packet(V.dave_buf + off, n, i == 0, i + 1 == chunks);
        off += n;
    }
    V.picture_id = (uint16_t)((V.picture_id + 1) & 0x7FFF);
    return true;
}

static void send_sender_report(void)
{
    uint8_t pkt[8 + 20 + 20];
    pkt[0] = 0x80;
    pkt[1] = 200;
    put16(pkt + 2, 6);
    put32(pkt + 4, V.video_ssrc);
    uint64_t now = uptime_ms();
    put32(pkt + 8, (uint32_t)(now / 1000));
    put32(pkt + 12, (uint32_t)(((now % 1000) << 32) / 1000));
    put32(pkt + 16, V.vts_base + (uint32_t)(now * 90));
    put32(pkt + 20, V.v_packets);
    put32(pkt + 24, V.v_octets);
    uint32_t nonce = V.tx_nonce++;
    uint8_t iv[12] = { 0 }, tag[16];
    put32(iv, nonce);
    mls_aes_gcm(true, V.key, 32, iv, 12, pkt, 8, pkt + 8, 20, tag);
    memcpy(pkt + 28, tag, 16);
    put32(pkt + 44, nonce);
    write(V.udp, pkt, sizeof(pkt));
}

static void pace_pump(void)
{
    uint64_t now = uptime_ms();
    int64_t rate = VIDEO_BITRATE * 3 / 8 / 1000;
    V.pace += (int64_t)(now - V.pace_at) * rate;
    if (V.pace > rate * 20)
        V.pace = rate * 20;
    V.pace_at = now;
    while (V.q_count > 0 && V.pace > 0)
    {
        size_t len = V.qlen[V.q_head];
        write(V.udp, V.queue + (size_t)V.q_head * MAX_PACKET, len);
        V.pace -= (int64_t)len;
        V.q_head = (V.q_head + 1) % VIDEO_QUEUE;
        V.q_count--;
    }
}

static void adjust_quality(size_t bytes, bool key)
{
    size_t target = VIDEO_BITRATE / 8 / VIDEO_FPS;
    if (key)
        target *= 6;
    if (bytes > target * 2)
        V.q += bytes > target * 4 ? 8 : 3;
    else if (bytes < target / 3)
        V.q--;
    if (V.q < Q_MIN)
        V.q = Q_MIN;
    if (V.q > Q_MAX)
        V.q = Q_MAX;
}

static void video_pump(void)
{
    if (!V.video_on || V.udp < 0)
        return;
    uint64_t now = uptime_ms();
    if (V.q_count > 0)
        pace_pump();

    int64_t rate = VIDEO_BITRATE / 8 / 1000;
    V.credit += (int64_t)(now - V.credit_at) * rate;
    if (V.credit > rate * 1000)
        V.credit = rate * 1000;
    V.credit_at = now;

    if (now >= V.next_sr)
    {
        send_sender_report();
        V.next_sr = now + SR_EVERY_MS;
    }
    if (now < V.next_frame || V.q_count > 0)
        return;
    V.next_frame = now + 1000 / VIDEO_FPS;

    bool key = (V.want_key && now - V.last_key >= KEY_MIN_MS) || !V.last_key || now - V.last_key >= KEY_EVERY_MS;
    if (V.credit < 0 && !key)
        return;
    int grabbed = screen_grab(V.screen);
    if (grabbed < 0)
        return;
    if (grabbed == 0 && !key && now - V.last_frame < IDLE_FRAME_MS)
        return;

    size_t len = screen_encode(V.screen, key, V.q, V.frame_buf, VIDEO_MAX_FRAME);
    if (len == 0)
    {
        V.q = V.q + 10 > Q_MAX ? Q_MAX : V.q + 10;
        request_key();
        return;
    }
    if (!send_video_frame(V.frame_buf, len, key))
    {
        request_key();
        return;
    }
    V.credit -= (int64_t)len;
    V.last_frame = now;
    if (key)
    {
        V.last_key = now;
        V.want_key = false;
    }
    adjust_quality(len, key);
    pace_pump();
}

static void start_video(void)
{
    if (V.video_on)
        return;
    if (!V.screen)
    {
        V.screen = screen_open(VIDEO_MAX_W, VIDEO_MAX_H);
        if (!V.screen)
            quit("Could not capture the screen.");
    }
    if (!V.queue)
        V.queue = malloc((size_t)VIDEO_QUEUE * MAX_PACKET);
    if (!V.frame_buf)
        V.frame_buf = malloc(VIDEO_MAX_FRAME);
    if (!V.dave_buf)
        V.dave_buf = malloc(VIDEO_MAX_FRAME + 64);
    if (!V.queue || !V.frame_buf || !V.dave_buf)
        quit("Out of memory for screen sharing.");

    char json[512];
    int w = screen_width(V.screen), h = screen_height(V.screen);
    snprintf(json, sizeof(json),
             "{\"op\":12,\"d\":{\"audio_ssrc\":%u,\"video_ssrc\":%u,\"rtx_ssrc\":%u,\"streams\":[{\"type\":\"video\","
             "\"rid\":\"100\",\"ssrc\":%u,\"active\":true,\"quality\":100,\"rtx_ssrc\":%u,\"max_bitrate\":%d,"
             "\"max_framerate\":%d,\"max_resolution\":{\"type\":\"fixed\",\"width\":%d,\"height\":%d}}]}}",
             V.ssrc, V.video_ssrc, V.rtx_ssrc, V.video_ssrc, V.rtx_ssrc, VIDEO_BITRATE, VIDEO_FPS, w, h);
    send_json(json);
    send_speaking(true);

    random_bytes(&V.vseq, sizeof(V.vseq));
    random_bytes(&V.vts_base, sizeof(V.vts_base));
    random_bytes(&V.picture_id, sizeof(V.picture_id));
    V.picture_id &= 0x7FFF;
    uint64_t now = uptime_ms();
    V.q = Q_START;
    V.credit = 0;
    V.credit_at = V.pace_at = now;
    V.pace = 0;
    V.next_frame = now;
    V.next_sr = now + SR_EVERY_MS;
    V.last_key = 0;
    V.want_key = true;
    V.video_on = true;
    ctl_send("video %dx%d", w, h);
}

static bool is_video_ssrc(uint32_t ssrc)
{
    for (int i = 0; i < V.n_vssrc; i++)
        if (V.vssrc[i] == ssrc)
            return true;
    return false;
}

static void add_video_ssrc(uint32_t ssrc)
{
    if (ssrc && !is_video_ssrc(ssrc) && V.n_vssrc < 8)
        V.vssrc[V.n_vssrc++] = ssrc;
}

static void send_pli(uint32_t media_ssrc)
{
    uint64_t now = uptime_ms();
    if (!V.have_key || V.udp < 0 || now - V.last_pli < PLI_MIN_MS)
        return;
    V.last_pli = now;
    uint8_t pkt[12 + 20];
    pkt[0] = 0x81;
    pkt[1] = 206;
    put16(pkt + 2, 2);
    put32(pkt + 4, V.ssrc);
    put32(pkt + 8, media_ssrc);
    uint32_t nonce = V.tx_nonce++;
    uint8_t iv[12] = { 0 }, tag[16];
    put32(iv, nonce);
    mls_aes_gcm(true, V.key, 32, iv, 12, pkt, 8, pkt + 8, 4, tag);
    memcpy(pkt + 12, tag, 16);
    put32(pkt + 28, nonce);
    write(V.udp, pkt, sizeof(pkt));
}

static void video_lost(uint32_t ssrc)
{
    V.rx_active = false;
    V.need_key = true;
    send_pli(ssrc);
}

static void video_frame_done(uint32_t ssrc)
{
    size_t out_len;
    if (!dave_decrypt(V.dave, V.watch_user, V.rx_buf, V.rx_len, V.dec_buf, &out_len))
    {
        video_lost(ssrc);
        return;
    }
    if (V.need_key && !vp8dec_is_keyframe(V.dec_buf, out_len))
    {
        send_pli(ssrc);
        return;
    }
    int r = vp8dec_decode(V.dec, V.dec_buf, out_len);
    if (r < 0)
    {
        video_lost(ssrc);
        return;
    }
    V.need_key = false;
    Vp8Image img;
    if (r == 1 && V.win && vp8dec_image(V.dec, &img))
    {
        watch_frame(V.win, &img);
        if (!V.shown_video)
        {
            V.shown_video = true;
            ctl_send("video %dx%d", img.width, img.height);
        }
    }
}

static void video_packet(uint32_t ssrc, uint16_t seq, uint32_t ts, bool marker, const uint8_t *p, size_t n)
{
    if (!V.dec || n < 1)
        return;
    bool gap = V.have_vseq && seq != (uint16_t)(V.last_vseq + 1);
    if (V.have_vseq && (uint16_t)(seq - V.last_vseq) > 0x8000)
        return;
    V.have_vseq = true;
    V.last_vseq = seq;

    size_t i = 1;
    bool start = (p[0] & 0x10) && (p[0] & 0x07) == 0;
    if (p[0] & 0x80)
    {
        if (n < 2)
            return;
        uint8_t x = p[1];
        i = 2;
        if (x & 0x80)
            i += (i < n && (p[i] & 0x80)) ? 2 : 1;
        if (x & 0x40)
            i++;
        if (x & 0x30)
            i++;
    }
    if (i > n)
        return;
    p += i;
    n -= i;

    if (start)
    {
        if (gap || V.rx_active)
            video_lost(ssrc);
        V.rx_active = true;
        V.rx_len = 0;
        V.rx_ts = ts;
    }
    else if (!V.rx_active || gap || ts != V.rx_ts)
    {
        if (gap || V.rx_active)
            video_lost(ssrc);
        return;
    }
    if (V.rx_len + n > RX_MAX_FRAME)
    {
        video_lost(ssrc);
        return;
    }
    memcpy(V.rx_buf + V.rx_len, p, n);
    V.rx_len += n;
    if (marker)
    {
        V.rx_active = false;
        video_frame_done(ssrc);
    }
}

static Speaker *speaker_for_ssrc(uint32_t ssrc)
{
    for (int i = 0; i < V.n_speakers; i++)
        if (V.speakers[i].ssrc == ssrc)
            return &V.speakers[i];
    return NULL;
}

static void speaker_free(Speaker *s)
{
    if (s->dec)
        opus_decoder_destroy(s->dec);
    if (s->fd >= 0)
        close(s->fd);
    if (s->talking)
        ctl_send("speaking %llu 0", (unsigned long long)s->user);
}

static void speaker_set(uint32_t ssrc, uint64_t user)
{
    Speaker *s = speaker_for_ssrc(ssrc);
    if (s)
    {
        s->user = user;
        return;
    }
    if (V.n_speakers == MAX_SPEAKERS)
        return;
    s = &V.speakers[V.n_speakers++];
    memset(s, 0, sizeof(*s));
    s->ssrc = ssrc;
    s->user = user;
    s->fd = -1;
}

static void speaker_remove_user(uint64_t user)
{
    for (int i = 0; i < V.n_speakers; i++)
        if (V.speakers[i].user == user)
        {
            speaker_free(&V.speakers[i]);
            V.speakers[i] = V.speakers[--V.n_speakers];
            i--;
        }
}

static void play(Speaker *s, const int16_t *pcm, int samples)
{
    if (V.deafened)
        return;
    if (s->fd < 0)
    {
        s->fd = open(AUDIO_DEVICE, O_WRONLY);
        if (s->fd < 0)
            return;
    }
    struct audio_state st;
    if (ioctl(s->fd, AUDIO_GET_STATE, &st) == 0 && st.play_queued > PLAY_MAX_QUEUE)
        return;
    write(s->fd, pcm, (size_t)samples * AUDIO_FRAME);
}

static void on_audio(Speaker *s, const uint8_t *opus, size_t len, uint16_t seq)
{
    uint64_t now = uptime_ms();
    if (!s->dec)
    {
        int err;
        s->dec = opus_decoder_create(AUDIO_RATE, AUDIO_CHANNELS, &err);
        if (!s->dec)
            return;
    }

    static int16_t pcm[5760 * AUDIO_CHANNELS];
    bool fresh = !s->talking || now - s->last_packet > TALK_TIMEOUT_MS;
    if (s->have_seq && !fresh)
    {
        uint16_t gap = (uint16_t)(seq - s->last_seq);
        if (gap == 0 || gap > 0x8000)
            return;
        for (uint16_t lost = 1; lost < gap && lost <= 3; lost++)
        {
            int n = opus_decode(s->dec, NULL, 0, pcm, FRAME_SAMPLES, 0);
            if (n > 0)
                play(s, pcm, n);
        }
    }
    s->last_seq = seq;
    s->have_seq = true;
    s->last_packet = now;

    bool silence = len == 3 && opus[0] == 0xF8 && opus[1] == 0xFF && opus[2] == 0xFE;
    if (silence)
        return;

    if (fresh)
    {
        memset(pcm, 0, FRAME_BYTES);
        play(s, pcm, FRAME_SAMPLES);
        play(s, pcm, FRAME_SAMPLES);
    }
    int n = opus_decode(s->dec, opus, (int)len, pcm, 5760, 0);
    if (n > 0)
        play(s, pcm, n);
    if (!s->talking)
    {
        s->talking = true;
        ctl_send("speaking %llu 1", (unsigned long long)s->user);
    }
}

static void handle_udp_packet(uint8_t *p, size_t n)
{
    if (!V.discovered)
    {
        if (n >= 74 && p[0] == 0 && p[1] == 2)
        {
            char ip[65];
            memcpy(ip, p + 8, 64);
            ip[64] = '\0';
            int port = p[72] << 8 | p[73];
            V.discovered = true;
            Buf b = { 0 };
            buf_puts(&b, "{\"op\":1,\"d\":{\"protocol\":\"udp\",");
            if (V.stream || V.watch_user)
                buf_printf(&b, "\"codecs\":[{\"name\":\"opus\",\"type\":\"audio\",\"priority\":1000,\"payload_type\":120},"
                               "{\"name\":\"VP8\",\"type\":\"video\",\"priority\":1000,\"payload_type\":%d,"
                               "\"rtx_payload_type\":%d,\"encode\":%s,\"decode\":true}],",
                           VIDEO_PT, VIDEO_RTX_PT, V.stream ? "true" : "false");
            buf_puts(&b, "\"data\":{\"address\":");
            buf_json_string(&b, ip);
            buf_printf(&b, ",\"port\":%d,\"mode\":\"%s\"}}}", port, MODE);
            send_json(b.data);
            buf_free(&b);
        }
        return;
    }
    if (!V.have_key || n < 12 + 20 || (p[0] >> 6) != 2)
        return;
    if (p[1] >= 200 && p[1] <= 206)
    {
        handle_rtcp(p, n);
        return;
    }
    if (V.stream)
        return;
    uint8_t pt = p[1] & 0x7F;
    uint32_t pkt_ssrc = get32(p + 8);
    if (V.watch_user && pt != VIDEO_PT && (is_video_ssrc(pkt_ssrc) || (pt != 0x78 && pt < 200)))
    {
        if (pt != VIDEO_RTX_PT)
        {
            V.foreign_packets++;
            V.foreign_pt = pt;
        }
        return;
    }
    bool video = V.watch_user && (is_video_ssrc(pkt_ssrc) || (pt == VIDEO_PT && !speaker_for_ssrc(pkt_ssrc)));
    if (video)
        V.video_packets++;
    if (pt != 0x78 && !video)
        return;

    size_t hdr = 12 + 4 * (size_t)(p[0] & 0x0F);
    size_t ext_words = 0;
    if (p[0] & 0x10)
    {
        if (n < hdr + 4)
            return;
        ext_words = (size_t)(p[hdr + 2] << 8 | p[hdr + 3]);
        hdr += 4;
    }
    if (n < hdr + 20)
        return;
    size_t ct_len = n - hdr - 20;
    uint8_t iv[12] = { 0 };
    memcpy(iv, p + n - 4, 4);
    if (!mls_aes_gcm_check(V.key, 32, iv, 12, p, hdr, p + hdr, ct_len, p + hdr + ct_len, 16))
        return;
    if (ext_words * 4 > ct_len)
        return;
    uint8_t *frame = p + hdr + ext_words * 4;
    size_t frame_len = ct_len - ext_words * 4;
    if ((p[0] & 0x20) && frame_len > 0)
    {
        size_t pad = frame[frame_len - 1];
        if (pad > frame_len)
            return;
        frame_len -= pad;
    }
    if (video)
    {
        add_video_ssrc(pkt_ssrc);
        video_packet(pkt_ssrc, (uint16_t)(p[2] << 8 | p[3]), get32(p + 4), (p[1] & 0x80) != 0, frame, frame_len);
        return;
    }

    uint32_t ssrc = get32(p + 8);
    uint16_t seq = (uint16_t)(p[2] << 8 | p[3]);
    Speaker *s = speaker_for_ssrc(ssrc);
    if (!s || !s->user)
        return;
    uint8_t opus[MAX_PACKET];
    size_t opus_len;
    if (!dave_decrypt(V.dave, s->user, frame, frame_len, opus, &opus_len))
        return;
    on_audio(s, opus, opus_len, seq);
}

static void udp_pump(void)
{
    if (V.udp < 0)
        return;
    for (int i = 0; i < 64; i++)
    {
        uint8_t pkt[MAX_PACKET];
        ssize_t n = read(V.udp, pkt, sizeof(pkt));
        if (n <= 0)
            break;
        handle_udp_packet(pkt, (size_t)n);
    }
}

static void send_discovery(void)
{
    uint8_t pkt[74] = { 0 };
    pkt[1] = 1;
    pkt[3] = 70;
    put32(pkt + 4, V.ssrc);
    write(V.udp, pkt, sizeof(pkt));
    V.discovery_tries++;
    V.discovery_at = uptime_ms();
}

static void on_ready(Json *d)
{
    V.ssrc = (uint32_t)json_int(d, "ssrc", 0);
    V.video_ssrc = V.ssrc + 1;
    V.rtx_ssrc = V.ssrc + 2;
    Json *streams = json_get(d, "streams");
    if (streams && streams->child)
    {
        V.video_ssrc = (uint32_t)json_int(streams->child, "ssrc", V.video_ssrc);
        V.rtx_ssrc = (uint32_t)json_int(streams->child, "rtx_ssrc", V.rtx_ssrc);
    }
    snprintf(V.ip, sizeof(V.ip), "%s", json_str(d, "ip") ? json_str(d, "ip") : "");
    V.port = (int)json_int(d, "port", 0);
    bool mode_ok = false;
    Json *modes = json_get(d, "modes");
    for (Json *m = modes ? modes->child : NULL; m; m = m->next)
        if (m->type == JSON_STRING && strcmp(m->str, MODE) == 0)
            mode_ok = true;
    if (!mode_ok)
        quit("The voice server does not offer AES-GCM encryption.");

    if (V.udp >= 0)
        close(V.udp);
    V.udp = socket(SOCK_UDP | SOCK_NONBLOCK);
    char addr[96];
    snprintf(addr, sizeof(addr), "%s:%d", V.ip, V.port);
    if (V.udp < 0 || connect(V.udp, addr) != 0)
        quit("Could not open a UDP socket to the voice server.");
    V.discovered = false;
    V.discovery_tries = 0;
    send_discovery();
}

static void open_mic(void)
{
    if (V.stream || V.mic >= 0 || V.muted || V.deafened || !V.have_key)
        return;
    V.mic = open(AUDIO_DEVICE, O_RDONLY);
}

static void close_mic(void)
{
    if (V.mic >= 0)
        close(V.mic);
    V.mic = -1;
    if (V.speaking)
    {
        V.speaking = false;
        V.silence_left = SILENCE_FRAMES;
        send_speaking(false);
        ctl_send("speaking %llu 0", (unsigned long long)V.user_id);
    }
}

static void on_session(Json *d)
{
    Json *key = json_get(d, "secret_key");
    int i = 0;
    for (Json *k = key ? key->child : NULL; k && i < 32; k = k->next)
        V.key[i++] = (uint8_t)k->num;
    if (i != 32)
        quit("The voice server sent a bad key.");
    V.have_key = true;
    random_bytes(&V.tx_nonce, sizeof(V.tx_nonce));
    random_bytes(&V.rtp_seq, sizeof(V.rtp_seq));
    random_bytes(&V.rtp_ts, sizeof(V.rtp_ts));
    dave_select_protocol(V.dave, (int)json_int(d, "dave_protocol_version", 0));
    ctl_send("state connected");
    if (V.stream)
    {
        const char *codec = json_str(d, "video_codec");
        if (codec && strcmp(codec, "VP8") != 0)
            printf("discord-voice: server picked video codec %s\n", codec);
        start_video();
    }
    if (V.watch_user)
    {
        send_json("{\"op\":15,\"d\":{\"any\":100}}");
        V.watch_at = uptime_ms();
        if (V.win)
            watch_status(V.win, "Waiting for video...");
    }
    open_mic();
}

static void handle_json(const char *text, size_t len)
{
    JsonDoc *doc = json_parse(text, len);
    Json *root = json_root(doc);
    if (!root)
    {
        json_free(doc);
        return;
    }
    Json *seq = json_get(root, "seq");
    if (seq && seq->type == JSON_NUMBER)
        V.seq_ack = (int64_t)seq->num;
    int op = (int)json_int(root, "op", -1);
    Json *d = json_get(root, "d");

    switch (op)
    {
        case 2:
            on_ready(d);
            break;
        case 4:
            on_session(d);
            break;
        case 5:
        {
            uint64_t user = json_id(d, "user_id");
            uint32_t ssrc = (uint32_t)json_int(d, "ssrc", 0);
            if (user && ssrc)
                speaker_set(ssrc, user);
            break;
        }
        case 12:
            if (V.watch_user)
            {
                uint64_t user = json_id(d, "user_id");
                uint32_t audio = (uint32_t)json_int(d, "audio_ssrc", 0);
                if (user && audio)
                    speaker_set(audio, user);
                if (!user || user == V.watch_user)
                {
                    add_video_ssrc((uint32_t)json_int(d, "video_ssrc", 0));
                    Json *streams = json_get(d, "streams");
                    for (Json *s = streams ? streams->child : NULL; s; s = s->next)
                        add_video_ssrc((uint32_t)json_int(s, "ssrc", 0));
                }
            }
            break;
        case 14:
            break;
        case 8:
            V.hb_interval = (uint64_t)json_int(d, "heartbeat_interval", 13750);
            V.next_hb = uptime_ms() + V.hb_interval / 2;
            break;
        case 9:
            V.resuming = false;
            V.resumes = 0;
            ctl_send("state connected");
            break;
        case 11:
        {
            Json *ids = json_get(d, "user_ids");
            uint64_t list[64];
            int n = 0;
            for (Json *u = ids ? ids->child : NULL; u && n < 64; u = u->next)
            {
                list[n] = json_as_id(u);
                ctl_send("join %llu", (unsigned long long)list[n]);
                n++;
            }
            dave_clients_connect(V.dave, list, n);
            request_key();
            break;
        }
        case 13:
        {
            uint64_t user = json_id(d, "user_id");
            dave_client_disconnect(V.dave, user);
            speaker_remove_user(user);
            ctl_send("leave %llu", (unsigned long long)user);
            break;
        }
        case 21:
            dave_prepare_transition(V.dave, (uint16_t)json_int(d, "transition_id", 0),
                                    (int)json_int(d, "protocol_version", 0));
            break;
        case 22:
            dave_execute_transition(V.dave, (uint16_t)json_int(d, "transition_id", 0));
            break;
        case 24:
            dave_prepare_epoch(V.dave, (uint64_t)json_int(d, "epoch", 0), (int)json_int(d, "protocol_version", 0));
            break;
        default:
            break;
    }
    json_free(doc);
}

static void handle_binary(const uint8_t *p, size_t len)
{
    if (len < 3)
        return;
    V.seq_ack = p[0] << 8 | p[1];
    dave_binary(V.dave, p[2], p + 3, len - 3);
}

static void on_ws_closed(void)
{
    int code = ws_close_code(V.ws);
    char why[192];
    snprintf(why, sizeof(why), "%s", ws_error(V.ws));
    printf("discord-voice: gateway closed (%d): %s\n", code, why);
    switch (code)
    {
        case 4004:
            quit("The voice server rejected the token.");
            break;
        case 4006:
        case 4009:
            quit("The voice session expired.");
            break;
        case 4011:
            quit("The voice server was not found.");
            break;
        case 4014:
            quit(NULL);
            break;
        case 4016:
            quit("The voice server does not support our encryption.");
            break;
        case 4017:
            quit("This call requires end-to-end encryption.");
            break;
        default:
            break;
    }
    if (!V.have_key)
        quit(why);
    if (++V.resumes > 5)
        quit("Lost the connection to the voice server.");
    V.resuming = true;
    ws_free(V.ws);
    V.ws = NULL;
    V.reconnect_at = uptime_ms() + 1000ULL * (uint64_t)V.resumes;
    ctl_send("state connecting");
}

static void ws_pump(void)
{
    if (!V.ws)
    {
        if (V.reconnect_at && uptime_ms() >= V.reconnect_at)
            connect_ws();
        return;
    }
    WsState st = ws_step(V.ws);
    if (st == WS_CONNECTING && uptime_ms() - V.connect_at > CONNECT_MS)
        st = WS_CLOSED;
    if (st == WS_OPEN && !V.identified)
        send_identify();
    char *msg;
    size_t len;
    bool binary;
    while (V.ws && ws_recv_msg(V.ws, &msg, &len, &binary))
    {
        if (binary)
            handle_binary((const uint8_t *)msg, len);
        else
            handle_json(msg, len);
        free(msg);
    }
    if (st == WS_CLOSED)
    {
        on_ws_closed();
        return;
    }
    uint64_t now = uptime_ms();
    if (st == WS_OPEN && V.hb_interval && now >= V.next_hb)
    {
        char json[96];
        snprintf(json, sizeof(json), "{\"op\":3,\"d\":{\"t\":%llu,\"seq_ack\":%lld}}", (unsigned long long)now,
                 (long long)V.seq_ack);
        send_json(json);
        V.next_hb = now + V.hb_interval;
    }
}

static void mic_frame(int16_t *pcm)
{
    uint64_t now = uptime_ms();
    int32_t peak = 0;
    for (int i = 0; i < FRAME_SAMPLES * AUDIO_CHANNELS; i++)
    {
        int32_t v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > peak)
            peak = v;
    }
    if (peak > VAD_LEVEL)
        V.last_voice = now;
    bool talk = now - V.last_voice < VAD_HANGOVER_MS;

    if (talk && !V.speaking)
    {
        V.speaking = true;
        send_speaking(true);
        ctl_send("speaking %llu 1", (unsigned long long)V.user_id);
    }
    else if (!talk && V.speaking)
    {
        V.speaking = false;
        V.silence_left = SILENCE_FRAMES;
        send_speaking(false);
        ctl_send("speaking %llu 0", (unsigned long long)V.user_id);
    }

    if (V.speaking)
    {
        uint8_t out[1276];
        int n = opus_encode(V.enc, pcm, FRAME_SAMPLES, out, sizeof(out));
        if (n > 0)
            send_rtp(out, (size_t)n);
    }
    else if (V.silence_left > 0)
    {
        static const uint8_t silence[3] = { 0xF8, 0xFF, 0xFE };
        send_rtp(silence, 3);
        V.silence_left--;
    }
    V.rtp_ts += FRAME_SAMPLES;
}

static void mic_pump(void)
{
    if (V.mic < 0)
        return;
    for (int i = 0; i < 4; i++)
    {
        struct audio_state st;
        if (ioctl(V.mic, AUDIO_GET_STATE, &st) != 0 || st.capture_avail < FRAME_BYTES)
            return;
        static int16_t pcm[FRAME_SAMPLES * AUDIO_CHANNELS];
        if (read(V.mic, pcm, FRAME_BYTES) != FRAME_BYTES)
            return;
        mic_frame(pcm);
    }
}

static void ctl_command(char *line)
{
    if (strncmp(line, "mute ", 5) == 0)
    {
        V.muted = atoi(line + 5) != 0;
        if (V.muted)
            close_mic();
        else
            open_mic();
    }
    else if (strncmp(line, "deaf ", 5) == 0)
    {
        V.deafened = atoi(line + 5) != 0;
        if (V.deafened)
        {
            close_mic();
            for (int i = 0; i < V.n_speakers; i++)
                if (V.speakers[i].fd >= 0)
                {
                    ioctl(V.speakers[i].fd, AUDIO_DROP, NULL);
                    close(V.speakers[i].fd);
                    V.speakers[i].fd = -1;
                }
        }
        else
        {
            open_mic();
        }
    }
    else if (strcmp(line, "quit") == 0)
    {
        quit(NULL);
    }
}

static void ctl_pump(void)
{
    char buf[256];
    ssize_t n = read(V.ctl, buf, sizeof(buf));
    if (n == 0)
        quit(NULL);
    if (n < 0)
        return;
    for (ssize_t i = 0; i < n; i++)
    {
        if (buf[i] == '\n')
        {
            V.ctl_in[V.ctl_len] = '\0';
            ctl_command(V.ctl_in);
            V.ctl_len = 0;
        }
        else if (V.ctl_len < sizeof(V.ctl_in) - 1)
        {
            V.ctl_in[V.ctl_len++] = buf[i];
        }
    }
}

static void tick(void)
{
    uint64_t now = uptime_ms();
    dave_tick(V.dave, now);
    if (V.udp >= 0 && !V.discovered && now - V.discovery_at > DISCOVERY_MS)
    {
        if (V.discovery_tries >= 5)
            quit("The voice server did not answer over UDP.");
        send_discovery();
    }
    for (int i = 0; i < V.n_speakers; i++)
    {
        Speaker *s = &V.speakers[i];
        if (s->talking && now - s->last_packet > TALK_TIMEOUT_MS)
        {
            s->talking = false;
            ctl_send("speaking %llu 0", (unsigned long long)s->user);
        }
    }
    if (V.watch_user && V.win && V.watch_at && !V.shown_video && !V.watch_notice)
    {
        const char *msg = NULL;
        char buf[200];
        if (V.foreign_packets > 30 && V.video_packets == 0)
        {
            const char *name = V.foreign_pt == 101 || V.foreign_pt == 102 ? "H.264"
                             : V.foreign_pt == 103 || V.foreign_pt == 104 ? "H.265"
                             : V.foreign_pt == 107 || V.foreign_pt == 108 ? "VP9"
                             : V.foreign_pt == 109 || V.foreign_pt == 110 ? "AV1" : NULL;
            if (name)
                snprintf(buf, sizeof(buf), "This stream is sent as %s, which StrawOS can't play yet.", name);
            else
                snprintf(buf, sizeof(buf), "This stream uses a video format StrawOS can't play yet (type %d).",
                         V.foreign_pt);
            msg = buf;
        }
        else if (now - V.watch_at > 10000)
            msg = V.video_packets ? "Video is arriving but could not be decoded." : "No video is arriving from the stream.";
        if (msg)
        {
            V.watch_notice = true;
            printf("discord-voice: %s (vp8 packets %u, other %u, last type %d)\n", msg, V.video_packets,
                   V.foreign_packets, V.foreign_pt);
            watch_status(V.win, msg);
        }
    }
    uint64_t epoch = dave_ready(V.dave) ? dave_epoch(V.dave) : 0;
    if (epoch != V.shown_epoch)
    {
        V.shown_epoch = epoch;
        ctl_send("e2ee %llu", (unsigned long long)epoch);
        request_key();
    }
}

static void usage(void)
{
    printf("discord-voice is started by the Discord client\n");
    exit(1);
}

int main(int argc, char **argv)
{
    const char *ctl_name = NULL;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const char *k = argv[i], *v = argv[i + 1];
        if (!strcmp(k, "--endpoint"))
            snprintf(V.endpoint, sizeof(V.endpoint), "%s", v);
        else if (!strcmp(k, "--server"))
            snprintf(V.server, sizeof(V.server), "%s", v);
        else if (!strcmp(k, "--session"))
            snprintf(V.session, sizeof(V.session), "%s", v);
        else if (!strcmp(k, "--token"))
            snprintf(V.token, sizeof(V.token), "%s", v);
        else if (!strcmp(k, "--user"))
            V.user_id = parse_snowflake(v);
        else if (!strcmp(k, "--channel"))
            V.channel_id = parse_snowflake(v);
        else if (!strcmp(k, "--ctl"))
            ctl_name = v;
        else if (!strcmp(k, "--mute"))
            V.muted = atoi(v) != 0;
        else if (!strcmp(k, "--deaf"))
            V.deafened = atoi(v) != 0;
        else if (!strcmp(k, "--stream"))
            V.stream = atoi(v) != 0;
        else if (!strcmp(k, "--watch"))
            V.watch_user = parse_snowflake(v);
        else if (!strcmp(k, "--title"))
            snprintf(V.watch_title, sizeof(V.watch_title), "%s", v);
    }
    if (!V.endpoint[0] || !V.server[0] || !V.session[0] || !V.token[0] || !V.user_id || !ctl_name)
        usage();

    V.udp = -1;
    V.mic = -1;
    V.ctl = socket(0);
    if (V.ctl < 0 || connect(V.ctl, ctl_name) != 0)
    {
        printf("discord-voice: cannot reach the client on %s\n", ctl_name);
        return 1;
    }

    int err;
    V.enc = opus_encoder_create(AUDIO_RATE, AUDIO_CHANNELS, OPUS_APPLICATION_VOIP, &err);
    if (!V.enc)
        quit("Could not start the Opus encoder.");
    opus_encoder_ctl(V.enc, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(V.enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(V.enc, OPUS_SET_PACKET_LOSS_PERC(5));

    DaveIo io = { NULL, dave_json, dave_bin, dave_log };
    V.dave = dave_new(V.user_id, V.channel_id, &io);
    if (!V.dave)
        quit("Could not start end-to-end encryption.");

    if (V.watch_user)
    {
        V.dec = vp8dec_new();
        V.rx_buf = malloc(RX_MAX_FRAME);
        V.dec_buf = malloc(RX_MAX_FRAME);
        V.win = watch_open(V.watch_title[0] ? V.watch_title : "Stream");
        if (!V.dec || !V.rx_buf || !V.dec_buf || !V.win)
            quit("Could not open the stream window.");
        V.need_key = true;
    }

    ctl_send("state connecting");
    connect_ws();

    for (;;)
    {
        struct pollfd fds[5];
        int n = 0;
        fds[n++] = (struct pollfd){ V.ctl, POLLIN, 0 };
        n += ws_pollfd(V.ws, &fds[n]);
        if (V.udp >= 0)
            fds[n++] = (struct pollfd){ V.udp, POLLIN, 0 };
        if (V.win)
            fds[n++] = (struct pollfd){ sx_fd(), POLLIN, 0 };
        int64_t timeout = ws_has_buffered(V.ws) ? 0 : 20;
        if (V.mic >= 0)
        {
            struct audio_state st;
            if (ioctl(V.mic, AUDIO_GET_STATE, &st) == 0)
            {
                int64_t missing = st.capture_avail >= FRAME_BYTES ? 0 : FRAME_BYTES - (int64_t)st.capture_avail;
                int64_t until = missing * 1000 / (AUDIO_RATE * AUDIO_FRAME) + 1;
                if (missing == 0)
                    until = 0;
                if (until < timeout)
                    timeout = until;
            }
        }
        if (V.video_on)
        {
            uint64_t now = uptime_ms();
            int64_t until = V.next_frame > now ? (int64_t)(V.next_frame - now) : 0;
            if (V.q_count > 0)
                until = 1;
            if (until < timeout)
                timeout = until;
        }
        poll(fds, (uint64_t)n, timeout);

        if (fds[0].revents & (POLLIN | POLLHUP))
            ctl_pump();
        ws_pump();
        udp_pump();
        mic_pump();
        video_pump();
        if (V.win && !watch_pump(V.win))
        {
            ctl_send("closed");
            quit(NULL);
        }
        tick();
    }
}
