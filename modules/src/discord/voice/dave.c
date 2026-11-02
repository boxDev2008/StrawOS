#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dave.h"
#include "mls.h"

#define OP_READY_FOR_TRANSITION   23
#define OP_EXTERNAL_SENDER        25
#define OP_KEY_PACKAGE            26
#define OP_PROPOSALS              27
#define OP_COMMIT_WELCOME         28
#define OP_ANNOUNCE_COMMIT        29
#define OP_WELCOME                30
#define OP_INVALID_COMMIT_WELCOME 31

#define MAX_USERS        128
#define MAX_PENDING      8
#define KEY_EXPIRY_MS    10000
#define PASSTHROUGH_MS   10000
#define EXPORT_LABEL     "Discord Secure Frames v0"
#define TAG_BYTES        8
#define MARKER           0xFA

typedef struct
{
    bool     set;
    uint8_t  base[16];
    uint32_t gen;
    uint8_t  key[16];
    bool     have_key;
}
Ratchet;

typedef struct
{
    uint64_t user;
    Ratchet  cur;
    Ratchet  old;
    uint64_t old_until;
    uint32_t last_nonce;
    bool     seen;
}
Receiver;

typedef struct
{
    uint16_t id;
    int      version;
    bool     used;
}
Pending;

struct Dave
{
    DaveIo      io;
    uint64_t    self;
    uint64_t    channel;
    MlsSession *mls;
    int         version;
    bool        downgraded;
    uint64_t    now;
    uint64_t    passthrough_until;

    uint64_t    users[MAX_USERS];
    int         n_users;

    Pending     pending[MAX_PENDING];

    Ratchet     send;
    Ratchet     send_next;
    uint32_t    nonce;

    Receiver    recv[MAX_USERS];
    int         n_recv;
};

static void say(Dave *d, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#include <stdarg.h>

static void say(Dave *d, const char *fmt, ...)
{
    if (!d->io.log)
        return;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    d->io.log(d->io.user, msg);
}

static void ratchet_init(Ratchet *r, const uint8_t base[16])
{
    memset(r, 0, sizeof(*r));
    memcpy(r->base, base, 16);
    r->set = true;
}

static bool ratchet_key(Ratchet *r, uint32_t gen, uint8_t key[16])
{
    if (!r->set)
        return false;
    if (!r->have_key || r->gen != gen)
    {
        if (gen > 4096)
            return false;
        uint8_t secret[32];
        size_t secret_len = 16;
        memcpy(secret, r->base, 16);
        for (uint32_t g = 0;; g++)
        {
            uint8_t ctx[4] = { (uint8_t)(g >> 24), (uint8_t)(g >> 16), (uint8_t)(g >> 8), (uint8_t)g };
            if (g == gen)
            {
                mls_expand_with_label(secret, secret_len, "key", ctx, 4, r->key, 16);
                break;
            }
            uint8_t next[32];
            mls_expand_with_label(secret, secret_len, "secret", ctx, 4, next, 32);
            memcpy(secret, next, 32);
            secret_len = 32;
        }
        r->gen = gen;
        r->have_key = true;
    }
    memcpy(key, r->key, 16);
    return true;
}

static bool export_base(Dave *d, uint64_t user, uint8_t base[16])
{
    uint8_t ctx[8];
    for (int i = 0; i < 8; i++)
        ctx[i] = (uint8_t)(user >> (8 * i));
    return mls_export(d->mls, EXPORT_LABEL, ctx, 8, base, 16);
}

static Receiver *receiver(Dave *d, uint64_t user, bool create)
{
    for (int i = 0; i < d->n_recv; i++)
        if (d->recv[i].user == user)
            return &d->recv[i];
    if (!create || d->n_recv >= MAX_USERS)
        return NULL;
    Receiver *r = &d->recv[d->n_recv++];
    memset(r, 0, sizeof(*r));
    r->user = user;
    return r;
}

static void send_transition(Dave *d, int op, uint16_t id)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"op\":%d,\"d\":{\"transition_id\":%u}}", op, (unsigned)id);
    d->io.send_json(d->io.user, json);
}

static void reinit(Dave *d)
{
    if (d->version <= 0)
    {
        mls_session_init(d->mls);
        return;
    }
    mls_session_init(d->mls);
    MlsBuf kp = { 0 };
    if (mls_key_package(d->mls, &kp))
    {
        d->io.send_binary(d->io.user, OP_KEY_PACKAGE, kp.data, kp.len);
        say(d, "dave: sent key package (%u bytes)", (unsigned)kp.len);
    }
    else
    {
        say(d, "dave: key package failed: %s", mls_error(d->mls));
    }
    mls_buf_free(&kp);
}

Dave *dave_new(uint64_t user_id, uint64_t channel_id, const DaveIo *io)
{
    Dave *d = calloc(1, sizeof(Dave));
    if (!d)
        return NULL;
    d->io = *io;
    d->self = user_id;
    d->channel = channel_id;
    d->mls = mls_session_new(user_id, channel_id);
    if (!d->mls)
    {
        free(d);
        return NULL;
    }
    d->users[d->n_users++] = user_id;
    return d;
}

void dave_free(Dave *d)
{
    if (!d)
        return;
    mls_session_free(d->mls);
    memset(d, 0, sizeof(*d));
    free(d);
}

int dave_protocol(const Dave *d)
{
    return d->version;
}

bool dave_ready(const Dave *d)
{
    return d->version > 0 && d->send.set;
}

uint64_t dave_epoch(const Dave *d)
{
    return mls_epoch(d->mls);
}

static void add_pending(Dave *d, uint16_t id, int version)
{
    for (int i = 0; i < MAX_PENDING; i++)
        if (d->pending[i].used && d->pending[i].id == id)
        {
            d->pending[i].version = version;
            return;
        }
    for (int i = 0; i < MAX_PENDING; i++)
        if (!d->pending[i].used)
        {
            d->pending[i] = (Pending){ id, version, true };
            return;
        }
}

static void install_epoch(Dave *d)
{
    uint64_t roster[MAX_USERS];
    int n = mls_roster(d->mls, roster, MAX_USERS);
    for (int i = 0; i < d->n_recv; i++)
    {
        Receiver *r = &d->recv[i];
        bool member = false;
        for (int j = 0; j < n; j++)
            if (roster[j] == r->user)
                member = true;
        if (!member && r->cur.set)
        {
            r->old = r->cur;
            r->old_until = d->now + KEY_EXPIRY_MS;
            memset(&r->cur, 0, sizeof(r->cur));
        }
    }
    for (int i = 0; i < n; i++)
    {
        uint8_t base[16];
        if (!export_base(d, roster[i], base))
            continue;
        if (roster[i] == d->self)
        {
            ratchet_init(&d->send_next, base);
            continue;
        }
        Receiver *r = receiver(d, roster[i], true);
        if (!r)
            continue;
        if (r->cur.set && memcmp(r->cur.base, base, 16) != 0)
        {
            r->old = r->cur;
            r->old_until = d->now + KEY_EXPIRY_MS;
        }
        ratchet_init(&r->cur, base);
        r->seen = false;
    }
    say(d, "dave: epoch %llu with %d member(s)", (unsigned long long)mls_epoch(d->mls), n);
}

static void switch_sender(Dave *d)
{
    if (d->send_next.set && (!d->send.set || memcmp(d->send.base, d->send_next.base, 16) != 0))
    {
        d->send = d->send_next;
        d->nonce = 0;
    }
}

static void execute(Dave *d, uint16_t id)
{
    int found = -1;
    for (int i = 0; i < MAX_PENDING; i++)
        if (d->pending[i].used && d->pending[i].id == id)
            found = i;
    if (found < 0)
        return;
    int version = d->pending[found].version;
    d->pending[found].used = false;

    bool was_downgraded = d->downgraded;
    d->version = version;
    if (version == 0)
    {
        d->downgraded = true;
        d->passthrough_until = UINT64_MAX;
        memset(&d->send, 0, sizeof(d->send));
        say(d, "dave: downgraded to transport encryption only");
        return;
    }
    d->downgraded = false;
    if (was_downgraded && id != 0)
        d->passthrough_until = d->now + PASSTHROUGH_MS;
    switch_sender(d);
    say(d, "dave: transition %u executed", (unsigned)id);
}

static void transition_ready(Dave *d, uint16_t id)
{
    add_pending(d, id, d->version > 0 ? d->version : DAVE_MAX_PROTOCOL);
    if (id == 0)
        execute(d, 0);
    else
        send_transition(d, OP_READY_FOR_TRANSITION, id);
}

static void invalid(Dave *d, uint16_t id, const char *what)
{
    say(d, "dave: invalid %s: %s", what, mls_error(d->mls));
    send_transition(d, OP_INVALID_COMMIT_WELCOME, id);
    reinit(d);
}

void dave_select_protocol(Dave *d, int version)
{
    d->version = version;
    if (version > DAVE_MAX_PROTOCOL)
        d->version = DAVE_MAX_PROTOCOL;
    if (d->version == 0)
    {
        d->passthrough_until = UINT64_MAX;
        return;
    }
    reinit(d);
}

void dave_prepare_transition(Dave *d, uint16_t id, int version)
{
    add_pending(d, id, version);
    if (version == 0)
        d->passthrough_until = UINT64_MAX;
    if (id == 0)
        execute(d, 0);
    else
        send_transition(d, OP_READY_FOR_TRANSITION, id);
}

void dave_execute_transition(Dave *d, uint16_t id)
{
    execute(d, id);
}

void dave_prepare_epoch(Dave *d, uint64_t epoch, int version)
{
    if (epoch != 1)
        return;
    d->version = version > DAVE_MAX_PROTOCOL ? DAVE_MAX_PROTOCOL : version;
    reinit(d);
}

void dave_clients_connect(Dave *d, const uint64_t *ids, int n)
{
    for (int i = 0; i < n; i++)
    {
        bool have = false;
        for (int j = 0; j < d->n_users; j++)
            if (d->users[j] == ids[i])
                have = true;
        if (!have && d->n_users < MAX_USERS)
            d->users[d->n_users++] = ids[i];
    }
}

void dave_client_disconnect(Dave *d, uint64_t id)
{
    for (int j = 0; j < d->n_users; j++)
        if (d->users[j] == id && id != d->self)
        {
            d->users[j] = d->users[--d->n_users];
            break;
        }
}

void dave_binary(Dave *d, int op, const uint8_t *p, size_t len)
{
    switch (op)
    {
        case OP_EXTERNAL_SENDER:
            if (!mls_set_external_sender(d->mls, p, len))
                say(d, "dave: bad external sender: %s", mls_error(d->mls));
            break;
        case OP_PROPOSALS:
        {
            MlsBuf out = { 0 };
            int r = mls_process_proposals(d->mls, p, len, d->users, d->n_users, &out);
            if (r > 0)
                d->io.send_binary(d->io.user, OP_COMMIT_WELCOME, out.data, out.len);
            else if (r < 0)
                say(d, "dave: proposals rejected: %s", mls_error(d->mls));
            mls_buf_free(&out);
            break;
        }
        case OP_ANNOUNCE_COMMIT:
        {
            if (len < 2)
                break;
            uint16_t id = (uint16_t)(p[0] << 8 | p[1]);
            int r = mls_process_commit(d->mls, p + 2, len - 2);
            if (r > 0)
            {
                install_epoch(d);
                transition_ready(d, id);
            }
            else if (r < 0)
            {
                invalid(d, id, "commit");
            }
            break;
        }
        case OP_WELCOME:
        {
            if (len < 2)
                break;
            uint16_t id = (uint16_t)(p[0] << 8 | p[1]);
            if (mls_process_welcome(d->mls, p + 2, len - 2, d->users, d->n_users))
            {
                install_epoch(d);
                transition_ready(d, id);
            }
            else
            {
                invalid(d, id, "welcome");
            }
            break;
        }
        default:
            break;
    }
}

void dave_tick(Dave *d, uint64_t now)
{
    d->now = now;
    for (int i = 0; i < d->n_recv; i++)
        if (d->recv[i].old.set && now > d->recv[i].old_until)
            memset(&d->recv[i].old, 0, sizeof(Ratchet));
    if (d->passthrough_until != UINT64_MAX && d->passthrough_until && now > d->passthrough_until)
        d->passthrough_until = 0;
}

static size_t leb_write(uint32_t v, uint8_t *out)
{
    size_t n = 0;
    do
    {
        uint8_t b = v & 0x7F;
        v >>= 7;
        out[n++] = b | (v ? 0x80 : 0);
    }
    while (v);
    return n;
}

static bool leb_read(const uint8_t *p, const uint8_t *end, uint64_t *v, const uint8_t **next)
{
    uint64_t r = 0;
    for (int shift = 0; p < end && shift < 64; shift += 7)
    {
        uint8_t b = *p++;
        r |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
        {
            *v = r;
            *next = p;
            return true;
        }
    }
    return false;
}

static void make_iv(uint32_t nonce, uint8_t iv[12])
{
    memset(iv, 0, 12);
    iv[8] = (uint8_t)nonce;
    iv[9] = (uint8_t)(nonce >> 8);
    iv[10] = (uint8_t)(nonce >> 16);
    iv[11] = (uint8_t)(nonce >> 24);
}

static bool is_silence(const uint8_t *f, size_t len)
{
    return len == 3 && f[0] == 0xF8 && f[1] == 0xFF && f[2] == 0xFE;
}

bool dave_encrypt(Dave *d, const uint8_t *frame, size_t len, uint8_t *out, size_t *out_len)
{
    if (d->version == 0 || is_silence(frame, len))
    {
        memcpy(out, frame, len);
        *out_len = len;
        return true;
    }
    if (!d->send.set)
        return false;

    uint32_t nonce = ++d->nonce;
    uint8_t key[16], iv[12], tag[16];
    if (!ratchet_key(&d->send, nonce >> 24, key))
        return false;
    make_iv(nonce, iv);
    memcpy(out, frame, len);
    mls_aes_gcm(true, key, 16, iv, 12, NULL, 0, out, len, tag);
    size_t n = len;
    memcpy(out + n, tag, TAG_BYTES);
    n += TAG_BYTES;
    size_t leb = leb_write(nonce, out + n);
    n += leb;
    out[n++] = (uint8_t)(TAG_BYTES + leb + 1 + 2);
    out[n++] = MARKER;
    out[n++] = MARKER;
    *out_len = n;
    return true;
}

typedef struct
{
    uint64_t offset;
    uint64_t size;
}
Range;

bool dave_decrypt(Dave *d, uint64_t sender, const uint8_t *frame, size_t len, uint8_t *out, size_t *out_len)
{
    bool protocol_frame = false;
    size_t supp = 0;
    if (len >= 3 + TAG_BYTES + 1 && frame[len - 1] == MARKER && frame[len - 2] == MARKER)
    {
        supp = frame[len - 3];
        protocol_frame = supp >= TAG_BYTES + 1 + 3 && supp <= len;
    }

    if (!protocol_frame)
    {
        if (is_silence(frame, len) || d->version == 0 || (d->passthrough_until && d->now <= d->passthrough_until))
        {
            memcpy(out, frame, len);
            *out_len = len;
            return true;
        }
        return false;
    }

    size_t body = len - supp;
    const uint8_t *tag = frame + body;
    const uint8_t *p = tag + TAG_BYTES, *end = frame + len - 3;
    uint64_t nonce64;
    if (!leb_read(p, end, &nonce64, &p) || nonce64 > UINT32_MAX)
        return false;
    uint32_t nonce = (uint32_t)nonce64;

    Range ranges[16];
    int n_ranges = 0;
    uint64_t last_end = 0;
    while (p < end)
    {
        if (n_ranges == 16)
            return false;
        Range *r = &ranges[n_ranges];
        if (!leb_read(p, end, &r->offset, &p) || !leb_read(p, end, &r->size, &p))
            return false;
        if (r->offset < last_end || r->offset + r->size > body)
            return false;
        last_end = r->offset + r->size;
        n_ranges++;
    }

    uint8_t *aad = malloc(body ? body : 1);
    uint8_t *ct = malloc(body ? body : 1);
    if (!aad || !ct)
    {
        free(aad);
        free(ct);
        return false;
    }
    size_t aad_len = 0, ct_len = 0, pos = 0;
    for (int i = 0; i <= n_ranges; i++)
    {
        size_t start = i < n_ranges ? (size_t)ranges[i].offset : body;
        memcpy(ct + ct_len, frame + pos, start - pos);
        ct_len += start - pos;
        if (i < n_ranges)
        {
            memcpy(aad + aad_len, frame + start, (size_t)ranges[i].size);
            aad_len += (size_t)ranges[i].size;
            pos = start + (size_t)ranges[i].size;
        }
    }

    Receiver *r = receiver(d, sender, false);
    bool ok = false;
    for (int attempt = 0; r && attempt < 2 && !ok; attempt++)
    {
        Ratchet *rt = attempt == 0 ? &r->cur : &r->old;
        uint8_t key[16], iv[12];
        if (!rt->set || !ratchet_key(rt, nonce >> 24, key))
            continue;
        make_iv(nonce, iv);
        uint8_t *tmp = malloc(ct_len ? ct_len : 1);
        if (!tmp)
            break;
        memcpy(tmp, ct, ct_len);
        if (mls_aes_gcm_check(key, 16, iv, 12, aad, aad_len, tmp, ct_len, tag, TAG_BYTES))
        {
            memcpy(ct, tmp, ct_len);
            ok = true;
        }
        free(tmp);
    }

    if (ok)
    {
        if (r->seen && nonce == r->last_nonce)
            ok = false;
        r->last_nonce = nonce;
        r->seen = true;
    }

    if (ok)
    {
        size_t a = 0, c = 0;
        pos = 0;
        for (int i = 0; i <= n_ranges; i++)
        {
            size_t start = i < n_ranges ? (size_t)ranges[i].offset : body;
            memcpy(out + pos, ct + c, start - pos);
            c += start - pos;
            pos = start;
            if (i < n_ranges)
            {
                memcpy(out + pos, aad + a, (size_t)ranges[i].size);
                a += (size_t)ranges[i].size;
                pos += (size_t)ranges[i].size;
            }
        }
        *out_len = body;
    }
    free(aad);
    free(ct);
    return ok;
}
