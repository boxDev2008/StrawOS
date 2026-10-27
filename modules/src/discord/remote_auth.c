#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bearssl.h"
#include "tls.h"
#include "discord.h"

#define RA_URL "wss://remote-auth-gateway.discord.gg/?v=2"

static Ws      *ws;
static RaState  st = RA_OFF;
static bool     have_key;
static br_rsa_private_key sk;
static br_rsa_public_key  pk;
static unsigned char kbuf_priv[BR_RSA_KBUF_PRIV_SIZE(2048)];
static unsigned char kbuf_pub[BR_RSA_KBUF_PUB_SIZE(2048)];
static char     fingerprint[160];
static char     user_name[128];
static char     user_avatar[256];
static char     token[256];
static char     err[192];
static uint64_t hb_interval;
static uint64_t next_hb;

static bool make_key(void)
{
    if (have_key)
        return true;
    br_hmac_drbg_context rng;
    unsigned char seed[32];
    tls_entropy(seed);
    br_hmac_drbg_init(&rng, &br_sha256_vtable, seed, sizeof(seed));
    br_rsa_keygen kg = br_rsa_keygen_get_default();
    if (!kg(&rng.vtable, &sk, kbuf_priv, &pk, kbuf_pub, 2048, 65537))
        return false;
    have_key = true;
    return true;
}

static size_t der_len(uint8_t *out, size_t len)
{
    if (len < 128)
    {
        out[0] = (uint8_t)len;
        return 1;
    }
    if (len < 256)
    {
        out[0] = 0x81;
        out[1] = (uint8_t)len;
        return 2;
    }
    out[0] = 0x82;
    out[1] = (uint8_t)(len >> 8);
    out[2] = (uint8_t)len;
    return 3;
}

static size_t der_int(uint8_t *out, const unsigned char *v, size_t len)
{
    while (len > 1 && v[0] == 0)
    {
        v++;
        len--;
    }
    bool pad = (v[0] & 0x80) != 0;
    size_t o = 0;
    out[o++] = 0x02;
    o += der_len(out + o, len + pad);
    if (pad)
        out[o++] = 0;
    memcpy(out + o, v, len);
    return o + len;
}

static size_t spki(uint8_t *out)
{
    uint8_t ints[600];
    size_t n = der_int(ints, pk.n, pk.nlen);
    n += der_int(ints + n, pk.e, pk.elen);

    uint8_t rsa[620];
    size_t r = 0;
    rsa[r++] = 0x30;
    r += der_len(rsa + r, n);
    memcpy(rsa + r, ints, n);
    r += n;

    static const uint8_t alg[] = { 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00 };
    uint8_t bits[640];
    size_t b = 0;
    bits[b++] = 0x03;
    b += der_len(bits + b, r + 1);
    bits[b++] = 0;
    memcpy(bits + b, rsa, r);
    b += r;

    size_t o = 0;
    out[o++] = 0x30;
    o += der_len(out + o, sizeof(alg) + b);
    memcpy(out + o, alg, sizeof(alg));
    o += sizeof(alg);
    memcpy(out + o, bits, b);
    return o + b;
}

static bool decrypt_b64(const char *b64, uint8_t *out, size_t *len)
{
    if (!b64)
        return false;
    uint8_t buf[512];
    size_t n = strlen(b64);
    if (n > 700)
        return false;
    size_t blen = base64_decode(b64, n, buf);
    br_rsa_oaep_decrypt dec = br_rsa_oaep_decrypt_get_default();
    if (!dec(&br_sha256_vtable, NULL, 0, &sk, buf, &blen))
        return false;
    memcpy(out, buf, blen);
    *len = blen;
    return true;
}

static void set_error(const char *msg)
{
    snprintf(err, sizeof(err), "%s", msg);
    st = RA_FAILED;
    ws_free(ws);
    ws = NULL;
}

void ra_start(void)
{
    ws_free(ws);
    ws = NULL;
    fingerprint[0] = user_name[0] = user_avatar[0] = token[0] = err[0] = '\0';
    hb_interval = 0;
    if (!make_key())
    {
        set_error("Couldn't create a login key.");
        return;
    }
    ws = ws_open(RA_URL, "https://discord.com");
    st = RA_STARTING;
}

void ra_stop(void)
{
    ws_free(ws);
    ws = NULL;
    st = RA_OFF;
}

static void on_login(int status, const uint8_t *body, size_t len, void *user)
{
    (void)user;
    if (st != RA_SCANNED)
        return;
    JsonDoc *doc = json_parse((const char *)body, len);
    const char *enc = json_str(json_root(doc), "encrypted_token");
    uint8_t out[512];
    size_t n = 0;
    if (status == 200 && decrypt_b64(enc, out, &n) && n < sizeof(token))
    {
        memcpy(token, out, n);
        token[n] = '\0';
        st = RA_DONE;
        ws_free(ws);
        ws = NULL;
    }
    else if (status == 400 && body && strstr((const char *)body, "captcha"))
    {
        set_error("Discord wants a captcha for this login. Use a token instead.");
    }
    else
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "Discord didn't accept the QR login (HTTP %d).", status);
        set_error(msg);
    }
    json_free(doc);
}

static void handle(const char *text, size_t len)
{
    JsonDoc *doc = json_parse(text, len);
    Json *root = json_root(doc);
    const char *op = json_str(root, "op");
    if (!op)
    {
        json_free(doc);
        return;
    }
    if (strcmp(op, "hello") == 0)
    {
        hb_interval = (uint64_t)json_int(root, "heartbeat_interval", 41250);
        next_hb = uptime_ms() + hb_interval;
        uint8_t der[700];
        size_t n = spki(der);
        char b64[1000];
        base64_encode(der, n, b64, false);
        Buf b = { 0 };
        buf_puts(&b, "{\"op\":\"init\",\"encoded_public_key\":");
        buf_json_string(&b, b64);
        buf_puts(&b, "}");
        ws_send(ws, b.data, b.len);
        buf_free(&b);
    }
    else if (strcmp(op, "nonce_proof") == 0)
    {
        uint8_t nonce[512];
        size_t n;
        if (!decrypt_b64(json_str(root, "encrypted_nonce"), nonce, &n))
        {
            json_free(doc);
            set_error("Couldn't decrypt Discord's login challenge.");
            return;
        }
        uint8_t hash[32];
        br_sha256_context h;
        br_sha256_init(&h);
        br_sha256_update(&h, nonce, n);
        br_sha256_out(&h, hash);
        char proof[64];
        base64_encode(hash, sizeof(hash), proof, true);
        Buf b = { 0 };
        buf_puts(&b, "{\"op\":\"nonce_proof\",\"proof\":");
        buf_json_string(&b, proof);
        buf_puts(&b, "}");
        ws_send(ws, b.data, b.len);
        buf_free(&b);
    }
    else if (strcmp(op, "pending_remote_init") == 0)
    {
        const char *fp = json_str(root, "fingerprint");
        snprintf(fingerprint, sizeof(fingerprint), "%s", fp ? fp : "");
        st = RA_WAITING;
    }
    else if (strcmp(op, "pending_ticket") == 0)
    {
        uint8_t data[512];
        size_t n;
        if (decrypt_b64(json_str(root, "encrypted_user_payload"), data, &n))
        {
            data[n] = '\0';
            char *parts[4] = { 0 };
            char *p = (char *)data;
            for (int i = 0; i < 4 && p; i++)
            {
                parts[i] = p;
                p = i < 3 ? strchr(p, ':') : NULL;
                if (p)
                    *p++ = '\0';
            }
            snprintf(user_name, sizeof(user_name), "%s", parts[3] ? parts[3] : "");
            if (parts[0])
                user_avatar_url(parse_snowflake(parts[0]), parts[2] && strcmp(parts[2], "0") != 0 ? parts[2] : NULL,
                                parts[1], user_avatar, sizeof(user_avatar));
        }
        st = RA_SCANNED;
    }
    else if (strcmp(op, "pending_login") == 0)
    {
        const char *ticket = json_str(root, "ticket");
        Buf b = { 0 };
        buf_puts(&b, "{\"ticket\":");
        buf_json_string(&b, ticket ? ticket : "");
        buf_puts(&b, "}");
        char url[300];
        snprintf(url, sizeof(url), "%s/users/@me/remote-auth/login", api_base);
        http_request("POST", url, "Content-Type: application/json\r\nOrigin: https://discord.com\r\n", b.data, b.len, on_login, NULL);
        buf_free(&b);
        st = RA_SCANNED;
    }
    else if (strcmp(op, "pending_finish") == 0)
    {
        uint8_t out[512];
        size_t n;
        if (decrypt_b64(json_str(root, "encrypted_token"), out, &n) && n < sizeof(token))
        {
            memcpy(token, out, n);
            token[n] = '\0';
            st = RA_DONE;
        }
    }
    else if (strcmp(op, "cancel") == 0)
    {
        json_free(doc);
        ra_start();
        return;
    }
    json_free(doc);
}

void ra_step(void)
{
    if (!ws)
        return;
    WsState s = ws_step(ws);
    char *msg;
    size_t len;
    while (ws && ws_recv(ws, &msg, &len))
    {
        handle(msg, len);
        free(msg);
    }
    if (!ws || st == RA_DONE || st == RA_FAILED)
        return;
    if (s == WS_CLOSED)
    {
        if (st == RA_SCANNED)
            return;
        int code = ws_close_code(ws);
        if (st == RA_WAITING && (code == 1000 || code == 4003))
            set_error("The QR code expired.");
        else
        {
            char msg2[192];
            snprintf(msg2, sizeof(msg2), "QR login unavailable: %s", ws_error(ws));
            set_error(msg2);
        }
        return;
    }
    if (hb_interval && uptime_ms() >= next_hb)
    {
        ws_send(ws, "{\"op\":\"heartbeat\"}", 18);
        next_hb = uptime_ms() + hb_interval;
    }
}

RaState ra_state(void)
{
    return st;
}

const char *ra_fingerprint(void)
{
    return fingerprint;
}

const char *ra_user(void)
{
    return user_name;
}

const char *ra_user_avatar(void)
{
    return user_avatar;
}

const char *ra_token(void)
{
    return token;
}

const char *ra_error(void)
{
    return err;
}

int ra_pollfds(struct pollfd *fds, int max)
{
    if (max < 1 || !ws)
        return 0;
    return ws_pollfd(ws, fds);
}

bool ra_has_buffered(void)
{
    return ws_has_buffered(ws);
}

uint64_t ra_next_deadline(void)
{
    return ws && hb_interval ? next_hb : 0;
}
