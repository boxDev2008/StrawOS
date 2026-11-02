#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bearssl.h>

#include "mls.h"

#define NH  32
#define NK  16
#define NN  12
#define NPK 65
#define NSK 32

#define SUITE          2
#define WIRE_PUBLIC    1
#define WIRE_WELCOME   3
#define CT_PROPOSAL    2
#define CT_COMMIT      3
#define SENDER_MEMBER  1
#define SENDER_EXTERNAL 2
#define PROP_ADD       1
#define PROP_REMOVE    3
#define SRC_KEY_PACKAGE 1
#define SRC_COMMIT     3
#define EXT_RATCHET_TREE    2
#define EXT_EXTERNAL_SENDERS 5

#define NODE_BLANK  0
#define NODE_LEAF   1
#define NODE_PARENT 2

#define MAX_DEPTH 32
#define MAX_QUEUE 64

static const uint8_t ZEROS[NH];

void mls_buf_free(MlsBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void w_put(MlsBuf *b, const void *data, size_t len)
{
    if (b->len + len > b->cap)
    {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + len)
            cap *= 2;
        uint8_t *p = realloc(b->data, cap);
        if (!p)
            abort();
        b->data = p;
        b->cap = cap;
    }
    if (len)
        memcpy(b->data + b->len, data, len);
    b->len += len;
}

static void w_u8(MlsBuf *b, uint8_t v) { w_put(b, &v, 1); }
static void w_u16(MlsBuf *b, uint16_t v) { uint8_t t[2] = { v >> 8, v }; w_put(b, t, 2); }
static void w_u32(MlsBuf *b, uint32_t v) { uint8_t t[4] = { v >> 24, v >> 16, v >> 8, v }; w_put(b, t, 4); }
static void w_u64(MlsBuf *b, uint64_t v) { w_u32(b, (uint32_t)(v >> 32)); w_u32(b, (uint32_t)v); }

static void w_varint(MlsBuf *b, size_t n)
{
    if (n < 0x40)
        w_u8(b, (uint8_t)n);
    else if (n < 0x4000)
        w_u16(b, (uint16_t)(0x4000 | n));
    else
        w_u32(b, (uint32_t)(0x80000000u | n));
}

static void w_opaque(MlsBuf *b, const void *data, size_t len)
{
    w_varint(b, len);
    w_put(b, data, len);
}

static void w_buf(MlsBuf *b, const MlsBuf *v)
{
    w_opaque(b, v->data, v->len);
}

typedef struct
{
    const uint8_t *p;
    const uint8_t *end;
    bool err;
}
Rd;

static Rd rd(const uint8_t *p, size_t len)
{
    Rd r = { p, p + len, false };
    return r;
}

static bool r_take(Rd *r, size_t n, const uint8_t **out)
{
    if (r->err || (size_t)(r->end - r->p) < n)
    {
        r->err = true;
        return false;
    }
    if (out)
        *out = r->p;
    r->p += n;
    return true;
}

static uint8_t r_u8(Rd *r) { const uint8_t *p; return r_take(r, 1, &p) ? p[0] : 0; }
static uint16_t r_u16(Rd *r) { const uint8_t *p; return r_take(r, 2, &p) ? (uint16_t)(p[0] << 8 | p[1]) : 0; }
static uint32_t r_u32(Rd *r)
{
    const uint8_t *p;
    return r_take(r, 4, &p) ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3] : 0;
}
static uint64_t r_u64(Rd *r) { uint64_t hi = r_u32(r); return hi << 32 | r_u32(r); }

static size_t r_varint(Rd *r)
{
    uint8_t first = r_u8(r);
    switch (first >> 6)
    {
        case 0: return first;
        case 1: return (size_t)(first & 0x3F) << 8 | r_u8(r);
        case 2:
        {
            size_t v = (size_t)(first & 0x3F) << 24;
            v |= (size_t)r_u8(r) << 16;
            v |= (size_t)r_u8(r) << 8;
            return v | r_u8(r);
        }
        default:
            r->err = true;
            return 0;
    }
}

static bool r_opaque(Rd *r, const uint8_t **p, size_t *len)
{
    size_t n = r_varint(r);
    *len = n;
    return r_take(r, n, p);
}

static Rd r_sub(Rd *r)
{
    const uint8_t *p = NULL;
    size_t n = 0;
    if (!r_opaque(r, &p, &n))
        return (Rd){ NULL, NULL, true };
    return rd(p, n);
}

static bool r_fixed(Rd *r, uint8_t *out, size_t want)
{
    const uint8_t *p;
    size_t n;
    if (!r_opaque(r, &p, &n) || n != want)
    {
        r->err = true;
        return false;
    }
    memcpy(out, p, n);
    return true;
}

static bool r_done(Rd *r) { return !r->err && r->p == r->end; }

static void sha256(const void *data, size_t len, uint8_t out[NH])
{
    br_sha256_context c;
    br_sha256_init(&c);
    br_sha256_update(&c, data, len);
    br_sha256_out(&c, out);
}

static void hmac(const uint8_t *key, size_t key_len, const void *data, size_t len, uint8_t out[NH])
{
    br_hmac_key_context kc;
    br_hmac_context hc;
    br_hmac_key_init(&kc, &br_sha256_vtable, key, key_len);
    br_hmac_init(&hc, &kc, 0);
    br_hmac_update(&hc, data, len);
    br_hmac_out(&hc, out);
}

static void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t out[NH])
{
    hmac(salt_len ? salt : ZEROS, salt_len ? salt_len : NH, ikm, ikm_len, out);
}

static void hkdf_expand(const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len,
                        uint8_t *out, size_t len)
{
    br_hmac_key_context kc;
    br_hmac_key_init(&kc, &br_sha256_vtable, prk, prk_len);
    uint8_t t[NH];
    size_t t_len = 0, done = 0;
    for (uint8_t i = 1; done < len; i++)
    {
        br_hmac_context hc;
        br_hmac_init(&hc, &kc, 0);
        br_hmac_update(&hc, t, t_len);
        br_hmac_update(&hc, info, info_len);
        br_hmac_update(&hc, &i, 1);
        br_hmac_out(&hc, t);
        t_len = NH;
        size_t n = len - done < NH ? len - done : NH;
        memcpy(out + done, t, n);
        done += n;
    }
}

static void w_label(MlsBuf *b, const char *label)
{
    size_t n = strlen(label);
    w_varint(b, 8 + n);
    w_put(b, "MLS 1.0 ", 8);
    w_put(b, label, n);
}

void mls_expand_with_label(const uint8_t *secret, size_t secret_len, const char *label,
                           const uint8_t *ctx, size_t ctx_len, uint8_t *out, uint16_t len)
{
    MlsBuf info = { 0 };
    w_u16(&info, len);
    w_label(&info, label);
    w_opaque(&info, ctx, ctx_len);
    hkdf_expand(secret, secret_len, info.data, info.len, out, len);
    mls_buf_free(&info);
}

static void derive_secret(const uint8_t secret[NH], const char *label, uint8_t out[NH])
{
    mls_expand_with_label(secret, NH, label, NULL, 0, out, NH);
}

static void ref_hash(const char *label, const uint8_t *data, size_t len, uint8_t out[NH])
{
    MlsBuf b = { 0 };
    w_opaque(&b, label, strlen(label));
    w_opaque(&b, data, len);
    sha256(b.data, b.len, out);
    mls_buf_free(&b);
}

void mls_aes_gcm(bool encrypt, const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len,
                 const uint8_t *aad, size_t aad_len, uint8_t *data, size_t len, uint8_t tag[16])
{
    br_aes_ct_ctr_keys ctr;
    br_gcm_context gc;
    br_aes_ct_ctr_init(&ctr, key, key_len);
    br_gcm_init(&gc, &ctr.vtable, br_ghash_ctmul64);
    br_gcm_reset(&gc, iv, iv_len);
    br_gcm_aad_inject(&gc, aad, aad_len);
    br_gcm_flip(&gc);
    br_gcm_run(&gc, encrypt, data, len);
    br_gcm_get_tag(&gc, tag);
}

bool mls_aes_gcm_check(const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len,
                       const uint8_t *aad, size_t aad_len, uint8_t *data, size_t len,
                       const uint8_t *tag, size_t tag_len)
{
    br_aes_ct_ctr_keys ctr;
    br_gcm_context gc;
    br_aes_ct_ctr_init(&ctr, key, key_len);
    br_gcm_init(&gc, &ctr.vtable, br_ghash_ctmul64);
    br_gcm_reset(&gc, iv, iv_len);
    br_gcm_aad_inject(&gc, aad, aad_len);
    br_gcm_flip(&gc);
    br_gcm_run(&gc, 0, data, len);
    return br_gcm_check_tag_trunc(&gc, tag, tag_len) == 1;
}

static const br_ec_impl *ec(void)
{
    return &br_ec_p256_m31;
}

static const uint8_t P256_ORDER[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
};

static bool ec_pub(const uint8_t priv[NSK], uint8_t pub[NPK])
{
    br_ec_private_key sk = { BR_EC_secp256r1, (unsigned char *)priv, NSK };
    br_ec_public_key pk;
    return br_ec_compute_pub(ec(), &pk, pub, &sk) == NPK;
}

static bool dh(const uint8_t priv[NSK], const uint8_t pub[NPK], uint8_t out[32])
{
    uint8_t point[NPK];
    memcpy(point, pub, NPK);
    if (!ec()->mul(point, NPK, priv, NSK, BR_EC_secp256r1))
        return false;
    memcpy(out, point + 1, 32);
    return true;
}

static const uint8_t KEM_SUITE[] = { 'K', 'E', 'M', 0x00, 0x10 };
static const uint8_t HPKE_SUITE[] = { 'H', 'P', 'K', 'E', 0x00, 0x10, 0x00, 0x01, 0x00, 0x01 };

static void labeled_extract(const uint8_t *suite, size_t suite_len, const uint8_t *salt, size_t salt_len,
                            const char *label, const uint8_t *ikm, size_t ikm_len, uint8_t out[NH])
{
    MlsBuf m = { 0 };
    w_put(&m, "HPKE-v1", 7);
    w_put(&m, suite, suite_len);
    w_put(&m, label, strlen(label));
    w_put(&m, ikm, ikm_len);
    hkdf_extract(salt, salt_len, m.data, m.len, out);
    mls_buf_free(&m);
}

static void labeled_expand(const uint8_t *suite, size_t suite_len, const uint8_t prk[NH], const char *label,
                           const uint8_t *info, size_t info_len, uint8_t *out, uint16_t len)
{
    MlsBuf m = { 0 };
    w_u16(&m, len);
    w_put(&m, "HPKE-v1", 7);
    w_put(&m, suite, suite_len);
    w_put(&m, label, strlen(label));
    w_put(&m, info, info_len);
    hkdf_expand(prk, NH, m.data, m.len, out, len);
    mls_buf_free(&m);
}

static bool derive_key_pair(const uint8_t *ikm, size_t ikm_len, uint8_t priv[NSK], uint8_t pub[NPK])
{
    uint8_t prk[NH];
    labeled_extract(KEM_SUITE, sizeof(KEM_SUITE), NULL, 0, "dkp_prk", ikm, ikm_len, prk);
    for (int counter = 0; counter < 256; counter++)
    {
        uint8_t c = (uint8_t)counter;
        labeled_expand(KEM_SUITE, sizeof(KEM_SUITE), prk, "candidate", &c, 1, priv, NSK);
        if (memcmp(priv, ZEROS, NSK) != 0 && memcmp(priv, P256_ORDER, NSK) < 0)
            return ec_pub(priv, pub);
    }
    return false;
}

static bool random_key_pair(uint8_t priv[NSK], uint8_t pub[NPK])
{
    uint8_t ikm[NH];
    random_bytes(ikm, sizeof(ikm));
    return derive_key_pair(ikm, sizeof(ikm), priv, pub);
}

static void hpke_key_schedule(const uint8_t shared[NH], const uint8_t *info, size_t info_len,
                              uint8_t key[NK], uint8_t nonce[NN])
{
    uint8_t ctx[1 + 2 * NH];
    ctx[0] = 0;
    labeled_extract(HPKE_SUITE, sizeof(HPKE_SUITE), NULL, 0, "psk_id_hash", NULL, 0, ctx + 1);
    labeled_extract(HPKE_SUITE, sizeof(HPKE_SUITE), NULL, 0, "info_hash", info, info_len, ctx + 1 + NH);
    uint8_t secret[NH];
    labeled_extract(HPKE_SUITE, sizeof(HPKE_SUITE), shared, NH, "secret", NULL, 0, secret);
    labeled_expand(HPKE_SUITE, sizeof(HPKE_SUITE), secret, "key", ctx, sizeof(ctx), key, NK);
    labeled_expand(HPKE_SUITE, sizeof(HPKE_SUITE), secret, "base_nonce", ctx, sizeof(ctx), nonce, NN);
}

static void kem_shared(const uint8_t dhv[32], const uint8_t enc[NPK], const uint8_t pk_r[NPK], uint8_t out[NH])
{
    uint8_t kem_ctx[2 * NPK];
    memcpy(kem_ctx, enc, NPK);
    memcpy(kem_ctx + NPK, pk_r, NPK);
    uint8_t prk[NH];
    labeled_extract(KEM_SUITE, sizeof(KEM_SUITE), NULL, 0, "eae_prk", dhv, 32, prk);
    labeled_expand(KEM_SUITE, sizeof(KEM_SUITE), prk, "shared_secret", kem_ctx, sizeof(kem_ctx), out, NH);
}

static void encrypt_context(MlsBuf *info, const char *label, const uint8_t *ctx, size_t ctx_len)
{
    w_label(info, label);
    w_opaque(info, ctx, ctx_len);
}

static bool encrypt_with_label(MlsBuf *out, const uint8_t pk[NPK], const char *label,
                               const uint8_t *ctx, size_t ctx_len, const uint8_t *pt, size_t pt_len)
{
    uint8_t sk_e[NSK], enc[NPK], dhv[32], shared[NH], key[NK], nonce[NN], tag[16];
    if (!random_key_pair(sk_e, enc) || !dh(sk_e, pk, dhv))
        return false;
    kem_shared(dhv, enc, pk, shared);
    MlsBuf info = { 0 };
    encrypt_context(&info, label, ctx, ctx_len);
    hpke_key_schedule(shared, info.data, info.len, key, nonce);
    mls_buf_free(&info);

    uint8_t *ct = malloc(pt_len + 16);
    if (!ct)
        return false;
    memcpy(ct, pt, pt_len);
    mls_aes_gcm(true, key, NK, nonce, NN, NULL, 0, ct, pt_len, tag);
    memcpy(ct + pt_len, tag, 16);
    w_opaque(out, enc, NPK);
    w_opaque(out, ct, pt_len + 16);
    free(ct);
    return true;
}

static bool decrypt_with_label(const uint8_t sk[NSK], const uint8_t pk[NPK], const char *label,
                               const uint8_t *ctx, size_t ctx_len, const uint8_t *enc, size_t enc_len,
                               const uint8_t *ct, size_t ct_len, MlsBuf *out)
{
    if (enc_len != NPK || ct_len < 16)
        return false;
    uint8_t dhv[32], shared[NH], key[NK], nonce[NN];
    if (!dh(sk, enc, dhv))
        return false;
    kem_shared(dhv, enc, pk, shared);
    MlsBuf info = { 0 };
    encrypt_context(&info, label, ctx, ctx_len);
    hpke_key_schedule(shared, info.data, info.len, key, nonce);
    mls_buf_free(&info);

    size_t n = ct_len - 16;
    out->len = 0;
    w_put(out, ct, n);
    if (!mls_aes_gcm_check(key, NK, nonce, NN, NULL, 0, out->data, n, ct + n, 16))
    {
        mls_buf_free(out);
        return false;
    }
    return true;
}

static void sign_content_hash(const char *label, const uint8_t *content, size_t len, uint8_t hash[NH])
{
    MlsBuf b = { 0 };
    w_label(&b, label);
    w_opaque(&b, content, len);
    sha256(b.data, b.len, hash);
    mls_buf_free(&b);
}

static size_t sign_with_label(const uint8_t sk[NSK], const char *label, const uint8_t *content, size_t len,
                              uint8_t sig[80])
{
    uint8_t hash[NH];
    sign_content_hash(label, content, len, hash);
    br_ec_private_key key = { BR_EC_secp256r1, (unsigned char *)sk, NSK };
    return br_ecdsa_i31_sign_asn1(ec(), &br_sha256_vtable, hash, &key, sig);
}

static bool verify_with_label(const uint8_t *pk, size_t pk_len, const char *label, const uint8_t *content,
                              size_t len, const uint8_t *sig, size_t sig_len)
{
    uint8_t hash[NH];
    sign_content_hash(label, content, len, hash);
    br_ec_public_key key = { BR_EC_secp256r1, (unsigned char *)pk, pk_len };
    return br_ecdsa_i31_vrfy_asn1(ec(), hash, NH, &key, sig, sig_len) == 1;
}

static bool signing_key_pair(uint8_t sk[NSK], uint8_t pk[NPK])
{
    for (int i = 0; i < 16; i++)
    {
        random_bytes(sk, NSK);
        if (memcmp(sk, ZEROS, NSK) != 0 && memcmp(sk, P256_ORDER, NSK) < 0)
            return ec_pub(sk, pk);
    }
    return false;
}

static uint32_t t_level(uint32_t x)
{
    uint32_t k = 0;
    while ((x >> k) & 1)
        k++;
    return k;
}

static uint32_t t_width(uint32_t leaves) { return leaves ? 2 * leaves - 1 : 0; }

static uint32_t t_root(uint32_t leaves)
{
    uint32_t w = t_width(leaves), k = 0;
    while ((1u << (k + 1)) <= w)
        k++;
    return (1u << k) - 1;
}

static uint32_t t_left(uint32_t x) { return x ^ (1u << (t_level(x) - 1)); }
static uint32_t t_right(uint32_t x) { return x ^ (3u << (t_level(x) - 1)); }

static uint32_t t_parent(uint32_t x)
{
    uint32_t k = t_level(x);
    uint32_t b = (x >> (k + 1)) & 1;
    return (x | (1u << k)) ^ (b << (k + 1));
}

static uint32_t t_sibling(uint32_t x)
{
    uint32_t p = t_parent(x);
    return x < p ? t_right(p) : t_left(p);
}

static bool t_in_subtree(uint32_t top, uint32_t x)
{
    uint32_t span = (1u << t_level(top)) - 1;
    return x + span >= top && x <= top + span;
}

static int t_direct_path(uint32_t x, uint32_t leaves, uint32_t *out)
{
    uint32_t root = t_root(leaves);
    int n = 0;
    while (x != root && n < MAX_DEPTH)
    {
        x = t_parent(x);
        out[n++] = x;
    }
    return n;
}

typedef struct
{
    uint8_t   type;
    uint8_t   enc[NPK];
    uint8_t   sig[NPK];
    uint64_t  uid;
    uint8_t  *raw;
    size_t    raw_len;
    uint8_t   ph[NH];
    uint8_t   ph_len;
    uint32_t *unmerged;
    uint32_t  n_unmerged;
    bool      has_priv;
    uint8_t   priv[NSK];
}
Node;

typedef struct
{
    Node    *n;
    uint32_t leaves;
}
Tree;

static void node_clear(Node *n)
{
    free(n->raw);
    free(n->unmerged);
    memset(n, 0, sizeof(*n));
}

static void tree_free(Tree *t)
{
    for (uint32_t i = 0; t->n && i < t_width(t->leaves); i++)
        node_clear(&t->n[i]);
    free(t->n);
    t->n = NULL;
    t->leaves = 0;
}

static bool node_copy(Node *dst, const Node *src)
{
    *dst = *src;
    dst->raw = NULL;
    dst->unmerged = NULL;
    if (src->raw)
    {
        dst->raw = malloc(src->raw_len);
        if (!dst->raw)
            return false;
        memcpy(dst->raw, src->raw, src->raw_len);
    }
    if (src->n_unmerged)
    {
        dst->unmerged = malloc(src->n_unmerged * sizeof(uint32_t));
        if (!dst->unmerged)
            return false;
        memcpy(dst->unmerged, src->unmerged, src->n_unmerged * sizeof(uint32_t));
    }
    return true;
}

static bool tree_copy(Tree *dst, const Tree *src)
{
    dst->leaves = src->leaves;
    dst->n = calloc(t_width(src->leaves), sizeof(Node));
    if (!dst->n)
        return false;
    for (uint32_t i = 0; i < t_width(src->leaves); i++)
        if (!node_copy(&dst->n[i], &src->n[i]))
            return false;
    return true;
}

static bool tree_resize(Tree *t, uint32_t leaves)
{
    uint32_t old_w = t_width(t->leaves), new_w = t_width(leaves);
    for (uint32_t i = new_w; i < old_w; i++)
        node_clear(&t->n[i]);
    Node *n = realloc(t->n, (new_w ? new_w : 1) * sizeof(Node));
    if (!n)
        return false;
    for (uint32_t i = old_w; i < new_w; i++)
        memset(&n[i], 0, sizeof(Node));
    t->n = n;
    t->leaves = leaves;
    return true;
}

typedef struct
{
    const uint8_t *raw;
    size_t         raw_len;
    const uint8_t *tbs;
    size_t         tbs_len;
    const uint8_t *enc;
    const uint8_t *sig;
    uint64_t       uid;
    uint8_t        source;
    const uint8_t *ph;
    size_t         ph_len;
    const uint8_t *signature;
    size_t         signature_len;
}
LeafView;

static bool parse_leaf(Rd *r, LeafView *v)
{
    memset(v, 0, sizeof(*v));
    const uint8_t *start = r->p, *p;
    size_t n;
    if (!r_opaque(r, &v->enc, &n) || n != NPK)
        return false;
    if (!r_opaque(r, &v->sig, &n) || n != NPK)
        return false;
    if (r_u16(r) != 1)
        return false;
    if (!r_opaque(r, &p, &n))
        return false;
    for (size_t i = 0; i < n && n == 8; i++)
        v->uid = v->uid << 8 | p[i];
    for (int i = 0; i < 5; i++)
        r_sub(r);
    v->source = r_u8(r);
    if (v->source == SRC_KEY_PACKAGE)
    {
        r_u64(r);
        r_u64(r);
    }
    else if (v->source == SRC_COMMIT)
    {
        if (!r_opaque(r, &v->ph, &v->ph_len))
            return false;
    }
    else if (v->source != 2)
    {
        return false;
    }
    r_sub(r);
    v->tbs = start;
    v->tbs_len = (size_t)(r->p - start);
    if (!r_opaque(r, &v->signature, &v->signature_len))
        return false;
    v->raw = start;
    v->raw_len = (size_t)(r->p - start);
    return !r->err;
}

static bool node_set_leaf(Node *n, const LeafView *v)
{
    node_clear(n);
    n->type = NODE_LEAF;
    memcpy(n->enc, v->enc, NPK);
    memcpy(n->sig, v->sig, NPK);
    n->uid = v->uid;
    n->raw = malloc(v->raw_len);
    if (!n->raw)
        return false;
    memcpy(n->raw, v->raw, v->raw_len);
    n->raw_len = v->raw_len;
    return true;
}

static void w_parent_node(MlsBuf *b, const Node *n)
{
    w_opaque(b, n->enc, NPK);
    w_opaque(b, n->ph, n->ph_len);
    w_varint(b, n->n_unmerged * 4);
    for (uint32_t i = 0; i < n->n_unmerged; i++)
        w_u32(b, n->unmerged[i]);
}

static void tree_hash(const Tree *t, uint32_t x, uint8_t out[NH])
{
    MlsBuf b = { 0 };
    const Node *n = &t->n[x];
    if ((x & 1) == 0)
    {
        w_u8(&b, NODE_LEAF);
        w_u32(&b, x / 2);
        if (n->type == NODE_LEAF)
        {
            w_u8(&b, 1);
            w_put(&b, n->raw, n->raw_len);
        }
        else
        {
            w_u8(&b, 0);
        }
    }
    else
    {
        uint8_t lh[NH], rh[NH];
        tree_hash(t, t_left(x), lh);
        tree_hash(t, t_right(x), rh);
        w_u8(&b, NODE_PARENT);
        if (n->type == NODE_PARENT)
        {
            w_u8(&b, 1);
            w_parent_node(&b, n);
        }
        else
        {
            w_u8(&b, 0);
        }
        w_opaque(&b, lh, NH);
        w_opaque(&b, rh, NH);
    }
    sha256(b.data, b.len, out);
    mls_buf_free(&b);
}

static void tree_root_hash(const Tree *t, uint8_t out[NH])
{
    tree_hash(t, t_root(t->leaves), out);
}

static int tree_resolution(const Tree *t, uint32_t x, const uint32_t *exclude, int n_exclude,
                           uint32_t *out, int max)
{
    const Node *n = &t->n[x];
    int count = 0;
    if (n->type == NODE_BLANK)
    {
        if (x & 1)
        {
            count += tree_resolution(t, t_left(x), exclude, n_exclude, out, max);
            count += tree_resolution(t, t_right(x), exclude, n_exclude, out + count, max - count);
        }
        return count;
    }
    if (n->type == NODE_LEAF)
        for (int i = 0; i < n_exclude; i++)
            if (exclude[i] == x / 2)
                return 0;
    if (count < max)
        out[count++] = x;
    for (uint32_t i = 0; i < n->n_unmerged; i++)
    {
        bool skip = false;
        for (int e = 0; e < n_exclude; e++)
            if (exclude[e] == n->unmerged[i])
                skip = true;
        if (!skip && count < max)
            out[count++] = n->unmerged[i] * 2;
    }
    return count;
}

static int tree_filtered_path(const Tree *t, uint32_t leaf, uint32_t *out)
{
    uint32_t dp[MAX_DEPTH], res[1];
    int n = t_direct_path(leaf * 2, t->leaves, dp), count = 0;
    uint32_t child = leaf * 2;
    for (int i = 0; i < n; i++)
    {
        uint32_t copath = t_sibling(child);
        if (tree_resolution(t, copath, NULL, 0, res, 1) > 0)
            out[count++] = dp[i];
        child = dp[i];
    }
    return count;
}

static uint32_t copath_child(uint32_t parent, uint32_t leaf_node)
{
    uint32_t l = t_left(parent);
    return t_in_subtree(l, leaf_node) ? t_right(parent) : l;
}

static uint32_t path_child(uint32_t parent, uint32_t leaf_node)
{
    uint32_t l = t_left(parent);
    return t_in_subtree(l, leaf_node) ? l : t_right(parent);
}

static void tree_blank(Tree *t, uint32_t x)
{
    node_clear(&t->n[x]);
}

static void tree_truncate(Tree *t)
{
    while (t->leaves > 1)
    {
        bool blank = true;
        for (uint32_t i = t->leaves / 2; i < t->leaves && blank; i++)
            if (t->n[i * 2].type != NODE_BLANK)
                blank = false;
        if (!blank)
            break;
        tree_resize(t, t->leaves / 2);
    }
}

static void tree_remove(Tree *t, uint32_t leaf)
{
    uint32_t dp[MAX_DEPTH];
    int n = t_direct_path(leaf * 2, t->leaves, dp);
    tree_blank(t, leaf * 2);
    for (int i = 0; i < n; i++)
        tree_blank(t, dp[i]);
    tree_truncate(t);
}

static bool tree_add(Tree *t, const LeafView *v, uint32_t *leaf_out)
{
    uint32_t leaf = t->leaves;
    for (uint32_t i = 0; i < t->leaves; i++)
        if (t->n[i * 2].type == NODE_BLANK)
        {
            leaf = i;
            break;
        }
    if (leaf == t->leaves && !tree_resize(t, t->leaves ? t->leaves * 2 : 1))
        return false;
    if (!node_set_leaf(&t->n[leaf * 2], v))
        return false;

    uint32_t dp[MAX_DEPTH];
    int n = t_direct_path(leaf * 2, t->leaves, dp);
    for (int i = 0; i < n; i++)
    {
        Node *p = &t->n[dp[i]];
        if (p->type != NODE_PARENT)
            continue;
        uint32_t *u = realloc(p->unmerged, (p->n_unmerged + 1) * sizeof(uint32_t));
        if (!u)
            return false;
        u[p->n_unmerged++] = leaf;
        p->unmerged = u;
    }
    *leaf_out = leaf;
    return true;
}

static void tree_set_parent_hashes(Tree *t, uint32_t leaf, const uint32_t *fdp, int n, uint8_t leaf_ph[NH])
{
    uint8_t ph[NH];
    size_t ph_len = 0;
    for (int i = n - 1; i >= 0; i--)
    {
        Node *p = &t->n[fdp[i]];
        memcpy(p->ph, ph, ph_len);
        p->ph_len = (uint8_t)ph_len;

        uint8_t sib[NH];
        tree_hash(t, copath_child(fdp[i], leaf * 2), sib);
        MlsBuf b = { 0 };
        w_opaque(&b, p->enc, NPK);
        w_opaque(&b, p->ph, p->ph_len);
        w_opaque(&b, sib, NH);
        sha256(b.data, b.len, ph);
        mls_buf_free(&b);
        ph_len = NH;
    }
    if (n == 0)
        memset(ph, 0, NH);
    memcpy(leaf_ph, ph, NH);
}

static void tree_serialize(const Tree *t, MlsBuf *out)
{
    uint32_t w = t_width(t->leaves);
    while (w > 0 && t->n[w - 1].type == NODE_BLANK)
        w--;
    MlsBuf b = { 0 };
    for (uint32_t i = 0; i < w; i++)
    {
        const Node *n = &t->n[i];
        if (n->type == NODE_BLANK)
        {
            w_u8(&b, 0);
            continue;
        }
        w_u8(&b, 1);
        w_u8(&b, n->type);
        if (n->type == NODE_LEAF)
            w_put(&b, n->raw, n->raw_len);
        else
            w_parent_node(&b, n);
    }
    w_buf(out, &b);
    mls_buf_free(&b);
}

static bool tree_parse(Rd *r, Tree *t)
{
    memset(t, 0, sizeof(*t));
    Rd v = r_sub(r);
    uint32_t count = 0, cap = 0;
    Node *nodes = NULL;
    while (!v.err && v.p < v.end)
    {
        if (count == cap)
        {
            cap = cap ? cap * 2 : 16;
            Node *nn = realloc(nodes, cap * sizeof(Node));
            if (!nn)
                goto fail;
            nodes = nn;
        }
        Node *n = &nodes[count];
        memset(n, 0, sizeof(*n));
        count++;
        if (r_u8(&v) == 0)
            continue;
        uint8_t type = r_u8(&v);
        if (type == NODE_LEAF)
        {
            if ((count - 1) & 1)
                goto fail;
            LeafView lv;
            if (!parse_leaf(&v, &lv) || !node_set_leaf(n, &lv))
                goto fail;
        }
        else if (type == NODE_PARENT)
        {
            if (!((count - 1) & 1))
                goto fail;
            n->type = NODE_PARENT;
            if (!r_fixed(&v, n->enc, NPK))
                goto fail;
            const uint8_t *p;
            size_t len;
            if (!r_opaque(&v, &p, &len) || len > NH)
                goto fail;
            memcpy(n->ph, p, len);
            n->ph_len = (uint8_t)len;
            Rd u = r_sub(&v);
            size_t nu = (size_t)(u.end - u.p) / 4;
            if (nu)
            {
                n->unmerged = malloc(nu * sizeof(uint32_t));
                if (!n->unmerged)
                    goto fail;
                for (size_t i = 0; i < nu; i++)
                    n->unmerged[i] = r_u32(&u);
                n->n_unmerged = (uint32_t)nu;
            }
            if (u.err)
                goto fail;
        }
        else
        {
            goto fail;
        }
    }
    if (v.err || count == 0 || !(count & 1))
        goto fail;
    uint32_t leaves = 1;
    while (t_width(leaves) < count)
        leaves *= 2;
    t->n = nodes;
    t->leaves = (count + 1) / 2;
    nodes = NULL;
    if (!tree_resize(t, leaves))
        goto fail;
    return true;
fail:
    if (nodes)
    {
        t->n = nodes;
        t->leaves = 0;
        for (uint32_t i = 0; i < count; i++)
            node_clear(&nodes[i]);
        free(nodes);
        t->n = NULL;
    }
    tree_free(t);
    return false;
}

typedef struct
{
    uint8_t  gid[32];
    size_t   gid_len;
    uint64_t epoch;
    Tree     tree;
    uint32_t me;
    MlsBuf   ext;
    uint8_t  cth[NH];
    size_t   cth_len;
    uint8_t  ith[NH];
    uint8_t  init_secret[NH];
    uint8_t  exporter[NH];
    uint8_t  confirm_key[NH];
    uint8_t  membership_key[NH];
    uint8_t  authenticator[NH];
}
Group;

static void group_free(Group *g)
{
    if (!g)
        return;
    tree_free(&g->tree);
    mls_buf_free(&g->ext);
    memset(g, 0, sizeof(*g));
    free(g);
}

static Group *group_copy(const Group *src)
{
    Group *g = calloc(1, sizeof(Group));
    if (!g)
        return NULL;
    *g = *src;
    g->tree.n = NULL;
    g->ext.data = NULL;
    g->ext.len = g->ext.cap = 0;
    w_put(&g->ext, src->ext.data, src->ext.len);
    if (!tree_copy(&g->tree, &src->tree))
    {
        group_free(g);
        return NULL;
    }
    return g;
}

static void group_context(const Group *g, uint64_t epoch, const uint8_t tree_hash_v[NH],
                          const uint8_t *cth, size_t cth_len, MlsBuf *out)
{
    w_u16(out, 1);
    w_u16(out, SUITE);
    w_opaque(out, g->gid, g->gid_len);
    w_u64(out, epoch);
    w_opaque(out, tree_hash_v, NH);
    w_opaque(out, cth, cth_len);
    w_put(out, g->ext.data, g->ext.len);
}

static void group_current_context(const Group *g, MlsBuf *out)
{
    uint8_t th[NH];
    tree_root_hash(&g->tree, th);
    group_context(g, g->epoch, th, g->cth, g->cth_len, out);
}

static void group_epoch_secret(Group *g, const uint8_t epoch_secret[NH])
{
    derive_secret(epoch_secret, "init", g->init_secret);
    derive_secret(epoch_secret, "exporter", g->exporter);
    derive_secret(epoch_secret, "confirm", g->confirm_key);
    derive_secret(epoch_secret, "membership", g->membership_key);
    derive_secret(epoch_secret, "authentication", g->authenticator);
}

static void group_from_joiner(Group *g, const uint8_t joiner[NH], const MlsBuf *ctx, uint8_t welcome_secret[NH])
{
    uint8_t member[NH], epoch_secret[NH];
    hkdf_extract(joiner, NH, ZEROS, NH, member);
    if (welcome_secret)
        derive_secret(member, "welcome", welcome_secret);
    mls_expand_with_label(member, NH, "epoch", ctx->data, ctx->len, epoch_secret, NH);
    group_epoch_secret(g, epoch_secret);
}

static void group_update_interim(Group *g, const uint8_t tag[NH])
{
    MlsBuf b = { 0 };
    w_put(&b, g->cth, g->cth_len);
    w_opaque(&b, tag, NH);
    sha256(b.data, b.len, g->ith);
    mls_buf_free(&b);
}

static bool group_external_sender(const Group *g, const uint8_t **es, size_t *es_len)
{
    Rd r = rd(g->ext.data, g->ext.len);
    Rd list = r_sub(&r);
    while (!list.err && list.p < list.end)
    {
        uint16_t type = r_u16(&list);
        Rd data = r_sub(&list);
        if (type != EXT_EXTERNAL_SENDERS)
            continue;
        Rd senders = r_sub(&data);
        if (senders.err)
            return false;
        *es = senders.p;
        *es_len = (size_t)(senders.end - senders.p);
        return true;
    }
    return false;
}

typedef struct
{
    uint8_t  ref[NH];
    uint16_t type;
    uint32_t removed;
    uint8_t *kp;
    size_t   kp_len;
}
Proposal;

struct MlsSession
{
    uint64_t uid;
    uint8_t  gid[8];

    uint8_t  sig_priv[NSK];
    uint8_t  sig_pub[NPK];

    bool     have_leaf;
    uint8_t  leaf_priv[NSK];
    uint8_t  leaf_pub[NPK];
    MlsBuf   leaf_raw;

    bool     have_kp;
    uint8_t  init_priv[NSK];
    uint8_t  init_pub[NPK];
    uint8_t  kp_ref[NH];

    MlsBuf   es;
    uint8_t  es_key[NPK];

    Group   *group;
    Group   *pending;

    Proposal queue[MAX_QUEUE];
    int      n_queue;

    MlsBuf   out_commit;
    Group   *out_state;

    char     err[160];
};

static bool fail(MlsSession *s, const char *msg)
{
    snprintf(s->err, sizeof(s->err), "%s", msg);
    return false;
}

const char *mls_error(const MlsSession *s)
{
    return s->err;
}

static void w_capabilities(MlsBuf *b)
{
    w_varint(b, 2);
    w_u16(b, 1);
    w_varint(b, 2);
    w_u16(b, SUITE);
    w_varint(b, 0);
    w_varint(b, 0);
    w_varint(b, 2);
    w_u16(b, 1);
}

static void w_leaf_common(MlsBuf *b, const uint8_t enc[NPK], const uint8_t sig[NPK], uint64_t uid, uint8_t source,
                          const uint8_t *ph, size_t ph_len)
{
    w_opaque(b, enc, NPK);
    w_opaque(b, sig, NPK);
    w_u16(b, 1);
    uint8_t id[8];
    for (int i = 0; i < 8; i++)
        id[i] = (uint8_t)(uid >> (56 - 8 * i));
    w_opaque(b, id, 8);
    w_capabilities(b);
    w_u8(b, source);
    if (source == SRC_KEY_PACKAGE)
    {
        w_u64(b, 0);
        w_u64(b, UINT64_MAX);
    }
    else if (source == SRC_COMMIT)
    {
        w_opaque(b, ph, ph_len);
    }
    w_varint(b, 0);
}

static bool build_leaf(MlsSession *s, const uint8_t enc[NPK], uint8_t source, const uint8_t *ph, size_t ph_len,
                       const uint8_t *gid, size_t gid_len, uint32_t leaf_index, MlsBuf *out)
{
    MlsBuf tbs = { 0 };
    w_leaf_common(&tbs, enc, s->sig_pub, s->uid, source, ph, ph_len);
    size_t common = tbs.len;
    if (source != SRC_KEY_PACKAGE)
    {
        w_opaque(&tbs, gid, gid_len);
        w_u32(&tbs, leaf_index);
    }
    uint8_t sig[80];
    size_t sig_len = sign_with_label(s->sig_priv, "LeafNodeTBS", tbs.data, tbs.len, sig);
    out->len = 0;
    w_put(out, tbs.data, common);
    w_opaque(out, sig, sig_len);
    mls_buf_free(&tbs);
    return sig_len > 0;
}

static void clear_queue(MlsSession *s)
{
    for (int i = 0; i < s->n_queue; i++)
        free(s->queue[i].kp);
    s->n_queue = 0;
}

static void clear_pending(MlsSession *s)
{
    group_free(s->pending);
    s->pending = NULL;
    group_free(s->out_state);
    s->out_state = NULL;
    mls_buf_free(&s->out_commit);
    clear_queue(s);
    s->have_kp = false;
    s->have_leaf = false;
    mls_buf_free(&s->leaf_raw);
}

static void create_pending_group(MlsSession *s)
{
    if (!s->have_leaf || !s->es.len)
        return;
    group_free(s->pending);
    Group *g = calloc(1, sizeof(Group));
    if (!g)
        return;
    memcpy(g->gid, s->gid, 8);
    g->gid_len = 8;
    if (!tree_resize(&g->tree, 1))
    {
        group_free(g);
        return;
    }
    LeafView lv;
    Rd r = rd(s->leaf_raw.data, s->leaf_raw.len);
    if (!parse_leaf(&r, &lv) || !node_set_leaf(&g->tree.n[0], &lv))
    {
        group_free(g);
        return;
    }
    g->tree.n[0].has_priv = true;
    memcpy(g->tree.n[0].priv, s->leaf_priv, NSK);

    MlsBuf ext = { 0 };
    w_u16(&ext, EXT_EXTERNAL_SENDERS);
    MlsBuf data = { 0 };
    w_buf(&data, &s->es);
    w_buf(&ext, &data);
    w_buf(&g->ext, &ext);
    mls_buf_free(&ext);
    mls_buf_free(&data);

    uint8_t epoch_secret[NH], tag[NH];
    random_bytes(epoch_secret, NH);
    group_epoch_secret(g, epoch_secret);
    hmac(g->confirm_key, NH, NULL, 0, tag);
    group_update_interim(g, tag);
    s->pending = g;
}

MlsSession *mls_session_new(uint64_t user_id, uint64_t group_id)
{
    MlsSession *s = calloc(1, sizeof(MlsSession));
    if (!s)
        return NULL;
    s->uid = user_id;
    for (int i = 0; i < 8; i++)
        s->gid[i] = (uint8_t)(group_id >> (56 - 8 * i));
    if (!signing_key_pair(s->sig_priv, s->sig_pub))
    {
        free(s);
        return NULL;
    }
    return s;
}

void mls_session_free(MlsSession *s)
{
    if (!s)
        return;
    clear_pending(s);
    group_free(s->group);
    mls_buf_free(&s->es);
    memset(s, 0, sizeof(*s));
    free(s);
}

void mls_session_init(MlsSession *s)
{
    clear_pending(s);
    group_free(s->group);
    s->group = NULL;
    s->err[0] = '\0';
    if (!random_key_pair(s->leaf_priv, s->leaf_pub) ||
        !build_leaf(s, s->leaf_pub, SRC_KEY_PACKAGE, NULL, 0, NULL, 0, 0, &s->leaf_raw))
    {
        fail(s, "could not create the leaf node");
        return;
    }
    s->have_leaf = true;
    create_pending_group(s);
}

bool mls_set_external_sender(MlsSession *s, const uint8_t *data, size_t len)
{
    if (s->group)
        return fail(s, "external sender changed after joining a group");
    Rd r = rd(data, len);
    if (!r_fixed(&r, s->es_key, NPK))
        return fail(s, "bad external sender key");
    if (r_u16(&r) != 1)
        return fail(s, "external sender has an unexpected credential type");
    r_sub(&r);
    if (!r_done(&r))
        return fail(s, "malformed external sender");
    s->es.len = 0;
    w_put(&s->es, data, len);
    create_pending_group(s);
    return true;
}

bool mls_key_package(MlsSession *s, MlsBuf *out)
{
    if (!s->have_leaf)
        return fail(s, "no leaf node");
    if (!random_key_pair(s->init_priv, s->init_pub))
        return fail(s, "could not create an init key");

    MlsBuf tbs = { 0 };
    w_u16(&tbs, 1);
    w_u16(&tbs, SUITE);
    w_opaque(&tbs, s->init_pub, NPK);
    w_put(&tbs, s->leaf_raw.data, s->leaf_raw.len);
    w_varint(&tbs, 0);
    uint8_t sig[80];
    size_t sig_len = sign_with_label(s->sig_priv, "KeyPackageTBS", tbs.data, tbs.len, sig);
    out->len = 0;
    w_put(out, tbs.data, tbs.len);
    w_opaque(out, sig, sig_len);
    mls_buf_free(&tbs);

    ref_hash("MLS 1.0 KeyPackage Reference", out->data, out->len, s->kp_ref);
    s->have_kp = true;
    return true;
}

bool mls_established(const MlsSession *s)
{
    return s->group != NULL;
}

uint64_t mls_epoch(const MlsSession *s)
{
    return s->group ? s->group->epoch : 0;
}

int mls_roster(const MlsSession *s, uint64_t *out, int max)
{
    if (!s->group)
        return 0;
    int n = 0;
    for (uint32_t i = 0; i < s->group->tree.leaves && n < max; i++)
        if (s->group->tree.n[i * 2].type == NODE_LEAF)
            out[n++] = s->group->tree.n[i * 2].uid;
    return n;
}

bool mls_export(const MlsSession *s, const char *label, const uint8_t *ctx, size_t ctx_len,
                uint8_t *out, size_t len)
{
    if (!s->group)
        return false;
    uint8_t secret[NH], hash[NH];
    mls_expand_with_label(s->group->exporter, NH, label, NULL, 0, secret, NH);
    sha256(ctx, ctx_len, hash);
    mls_expand_with_label(secret, NH, "exported", hash, NH, out, (uint16_t)len);
    return true;
}

bool mls_epoch_authenticator(const MlsSession *s, uint8_t out[32])
{
    if (!s->group)
        return false;
    memcpy(out, s->group->authenticator, NH);
    return true;
}

typedef struct
{
    const uint8_t *content;
    size_t         content_len;
    const uint8_t *gid;
    size_t         gid_len;
    uint64_t       epoch;
    uint8_t        sender_type;
    uint32_t       sender;
    uint8_t        content_type;
    const uint8_t *body;
    size_t         body_len;
    const uint8_t *sig;
    size_t         sig_len;
    const uint8_t *tag;
    size_t         tag_len;
    const uint8_t *mtag;
    size_t         mtag_len;
}
PublicMessage;

static bool skip_key_package(Rd *r)
{
    r_u16(r);
    r_u16(r);
    r_sub(r);
    LeafView lv;
    if (!parse_leaf(r, &lv))
        return false;
    r_sub(r);
    r_sub(r);
    return !r->err;
}

static bool skip_proposal(Rd *r, uint16_t *type)
{
    *type = r_u16(r);
    switch (*type)
    {
        case PROP_ADD:
            return skip_key_package(r);
        case PROP_REMOVE:
            r_u32(r);
            return !r->err;
        default:
            return false;
    }
}

static bool skip_update_path(Rd *r)
{
    LeafView lv;
    if (!parse_leaf(r, &lv))
        return false;
    r_sub(r);
    return !r->err;
}

static bool parse_public_message(const uint8_t *data, size_t len, PublicMessage *m)
{
    memset(m, 0, sizeof(*m));
    Rd r = rd(data, len);
    if (r_u16(&r) != 1 || r_u16(&r) != WIRE_PUBLIC)
        return false;
    m->content = r.p;
    if (!r_opaque(&r, &m->gid, &m->gid_len))
        return false;
    m->epoch = r_u64(&r);
    m->sender_type = r_u8(&r);
    if (m->sender_type == SENDER_MEMBER || m->sender_type == SENDER_EXTERNAL)
        m->sender = r_u32(&r);
    else
        return false;
    r_sub(&r);
    m->content_type = r_u8(&r);
    m->body = r.p;
    if (m->content_type == CT_PROPOSAL)
    {
        uint16_t type;
        if (!skip_proposal(&r, &type))
            return false;
    }
    else if (m->content_type == CT_COMMIT)
    {
        r_sub(&r);
        uint8_t has_path = r_u8(&r);
        if (has_path == 1 && !skip_update_path(&r))
            return false;
        if (has_path > 1)
            return false;
    }
    else
    {
        return false;
    }
    m->body_len = (size_t)(r.p - m->body);
    m->content_len = (size_t)(r.p - m->content);
    if (!r_opaque(&r, &m->sig, &m->sig_len))
        return false;
    if (m->content_type == CT_COMMIT && !r_opaque(&r, &m->tag, &m->tag_len))
        return false;
    if (m->sender_type == SENDER_MEMBER && !r_opaque(&r, &m->mtag, &m->mtag_len))
        return false;
    return r_done(&r);
}

static void w_content_tbs(MlsBuf *b, const PublicMessage *m, const MlsBuf *ctx)
{
    w_u16(b, 1);
    w_u16(b, WIRE_PUBLIC);
    w_put(b, m->content, m->content_len);
    if (m->sender_type == SENDER_MEMBER)
        w_put(b, ctx->data, ctx->len);
}

static void w_auth(MlsBuf *b, const PublicMessage *m)
{
    w_opaque(b, m->sig, m->sig_len);
    if (m->content_type == CT_COMMIT)
        w_opaque(b, m->tag, m->tag_len);
}

static bool check_external_proposal(MlsSession *s, const Group *base, const PublicMessage *m)
{
    if (m->sender_type != SENDER_EXTERNAL || m->sender != 0)
        return fail(s, "proposal is not from the external sender");
    if (m->content_type != CT_PROPOSAL)
        return fail(s, "expected a proposal");
    if (m->epoch != base->epoch)
        return fail(s, "proposal epoch mismatch");
    if (m->gid_len != base->gid_len || memcmp(m->gid, base->gid, m->gid_len) != 0)
        return fail(s, "proposal for another group");
    MlsBuf tbs = { 0 };
    w_content_tbs(&tbs, m, NULL);
    bool ok = verify_with_label(s->es_key, NPK, "FramedContentTBS", tbs.data, tbs.len, m->sig, m->sig_len);
    mls_buf_free(&tbs);
    if (!ok)
        return fail(s, "bad external sender signature on proposal");
    return true;
}

static bool user_recognized(uint64_t uid, const uint64_t *users, int n)
{
    for (int i = 0; i < n; i++)
        if (users[i] == uid)
            return true;
    return false;
}

static bool queue_proposal(MlsSession *s, const PublicMessage *m, const uint64_t *users, int n_users)
{
    if (s->n_queue >= MAX_QUEUE)
        return fail(s, "too many queued proposals");
    Proposal *p = &s->queue[s->n_queue];
    memset(p, 0, sizeof(*p));

    Rd r = rd(m->body, m->body_len);
    p->type = r_u16(&r);
    if (p->type == PROP_ADD)
    {
        const uint8_t *kp = r.p;
        r_u16(&r);
        if (r_u16(&r) != SUITE)
            return fail(s, "key package uses another cipher suite");
        r_sub(&r);
        LeafView lv;
        if (!parse_leaf(&r, &lv))
            return fail(s, "malformed key package");
        size_t tbs_len = (size_t)(r.p - kp);
        r_sub(&r);
        tbs_len = (size_t)(r.p - kp);
        const uint8_t *sig;
        size_t sig_len;
        if (!r_opaque(&r, &sig, &sig_len) || !r_done(&r))
            return fail(s, "malformed key package");
        if (!verify_with_label(lv.sig, NPK, "KeyPackageTBS", kp, tbs_len, sig, sig_len))
            return fail(s, "bad key package signature");
        if (!user_recognized(lv.uid, users, n_users))
            return fail(s, "add proposal for an unrecognized user");
        p->kp_len = (size_t)(r.p - kp);
        p->kp = malloc(p->kp_len);
        if (!p->kp)
            return fail(s, "out of memory");
        memcpy(p->kp, kp, p->kp_len);
    }
    else if (p->type == PROP_REMOVE)
    {
        p->removed = r_u32(&r);
    }
    else
    {
        return fail(s, "unexpected proposal type");
    }

    MlsBuf ac = { 0 };
    w_u16(&ac, WIRE_PUBLIC);
    w_put(&ac, m->content, m->content_len);
    w_auth(&ac, m);
    ref_hash("MLS 1.0 Proposal Reference", ac.data, ac.len, p->ref);
    mls_buf_free(&ac);
    s->n_queue++;
    return true;
}

typedef struct
{
    uint32_t leaf;
    uint8_t  init_key[NPK];
    uint8_t  kp_ref[NH];
}
Added;

static bool apply_proposals(MlsSession *s, Group *g, Proposal **props, int n, Added *added, int *n_added,
                            bool *has_remove)
{
    *n_added = 0;
    *has_remove = false;
    for (int i = 0; i < n; i++)
    {
        if (props[i]->type != PROP_REMOVE)
            continue;
        uint32_t leaf = props[i]->removed;
        if (leaf >= g->tree.leaves || g->tree.n[leaf * 2].type != NODE_LEAF)
            return fail(s, "remove proposal for an empty leaf");
        if (leaf == g->me)
            return fail(s, "we were removed from the group");
        tree_remove(&g->tree, leaf);
        *has_remove = true;
    }
    for (int i = 0; i < n; i++)
    {
        if (props[i]->type != PROP_ADD)
            continue;
        Rd r = rd(props[i]->kp, props[i]->kp_len);
        r_u16(&r);
        r_u16(&r);
        Added *a = &added[(*n_added)++];
        if (!r_fixed(&r, a->init_key, NPK))
            return fail(s, "bad init key");
        LeafView lv;
        if (!parse_leaf(&r, &lv))
            return fail(s, "bad leaf in key package");
        for (uint32_t l = 0; l < g->tree.leaves; l++)
            if (g->tree.n[l * 2].type == NODE_LEAF && g->tree.n[l * 2].uid == lv.uid)
                return fail(s, "duplicate user in group");
        if (!tree_add(&g->tree, &lv, &a->leaf))
            return fail(s, "out of memory");
        ref_hash("MLS 1.0 KeyPackage Reference", props[i]->kp, props[i]->kp_len, a->kp_ref);
    }
    return true;
}

static bool finish_epoch(MlsSession *s, Group *ng, const Group *old, const uint8_t *content, size_t content_len,
                         const uint8_t *sig, size_t sig_len, const uint8_t commit_secret[NH],
                         uint8_t joiner[NH], uint8_t welcome_secret[NH], uint8_t tag[NH])
{
    (void)s;
    MlsBuf cthi = { 0 };
    w_put(&cthi, old->ith, NH);
    w_u16(&cthi, WIRE_PUBLIC);
    w_put(&cthi, content, content_len);
    w_opaque(&cthi, sig, sig_len);
    sha256(cthi.data, cthi.len, ng->cth);
    ng->cth_len = NH;
    mls_buf_free(&cthi);

    ng->epoch = old->epoch + 1;
    MlsBuf ctx = { 0 };
    group_current_context(ng, &ctx);

    uint8_t t[NH];
    hkdf_extract(old->init_secret, NH, commit_secret, NH, t);
    mls_expand_with_label(t, NH, "joiner", ctx.data, ctx.len, joiner, NH);
    group_from_joiner(ng, joiner, &ctx, welcome_secret);
    mls_buf_free(&ctx);

    hmac(ng->confirm_key, NH, ng->cth, NH, tag);
    group_update_interim(ng, tag);
    return true;
}

static bool set_path_keys(Tree *t, const uint32_t *fdp, int from, int n, const uint8_t ps_in[NH],
                          uint8_t commit_secret[NH])
{
    uint8_t ps[NH];
    bool match = true;
    memcpy(ps, ps_in, NH);
    for (int k = from; k < n; k++)
    {
        uint8_t node_secret[NH], priv[NSK], pub[NPK];
        derive_secret(ps, "node", node_secret);
        derive_key_pair(node_secret, NH, priv, pub);
        Node *p = &t->n[fdp[k]];
        if (p->type != NODE_PARENT || memcmp(p->enc, pub, NPK) != 0)
            match = false;
        p->has_priv = true;
        memcpy(p->priv, priv, NSK);
        uint8_t next[NH];
        derive_secret(ps, "path", next);
        memcpy(ps, next, NH);
    }
    if (commit_secret)
        memcpy(commit_secret, ps, NH);
    return match;
}

typedef struct
{
    uint64_t       epoch;
    const uint8_t *cth;
    size_t         cth_len;
}
PathContext;

static bool create_update_path(MlsSession *s, Group *g, const uint32_t *exclude, int n_exclude,
                               const PathContext *pc, MlsBuf *path, uint8_t path_secrets[][NH],
                               uint32_t *fdp, int *n_fdp_out, uint8_t commit_secret[NH])
{
    uint32_t me = g->me, dp[MAX_DEPTH];
    int n_dp = t_direct_path(me * 2, g->tree.leaves, dp);
    int n_fdp = tree_filtered_path(&g->tree, me, fdp);
    for (int i = 0; i < n_dp; i++)
        tree_blank(&g->tree, dp[i]);

    uint8_t leaf_priv[NSK], leaf_pub[NPK];
    if (!random_key_pair(leaf_priv, leaf_pub))
        return fail(s, "could not create a leaf key");
    random_bytes(path_secrets[0], NH);
    for (int k = 0; k < n_fdp; k++)
    {
        Node *p = &g->tree.n[fdp[k]];
        uint8_t node_secret[NH];
        derive_secret(path_secrets[k], "node", node_secret);
        p->type = NODE_PARENT;
        if (!derive_key_pair(node_secret, NH, p->priv, p->enc))
            return fail(s, "could not derive a path key");
        p->has_priv = true;
        derive_secret(path_secrets[k], "path", path_secrets[k + 1]);
    }
    memcpy(commit_secret, path_secrets[n_fdp], NH);

    uint8_t leaf_ph[NH];
    tree_set_parent_hashes(&g->tree, me, fdp, n_fdp, leaf_ph);
    MlsBuf leaf = { 0 };
    build_leaf(s, leaf_pub, SRC_COMMIT, leaf_ph, NH, g->gid, g->gid_len, me, &leaf);
    LeafView lv;
    Rd lr = rd(leaf.data, leaf.len);
    if (!parse_leaf(&lr, &lv) || !node_set_leaf(&g->tree.n[me * 2], &lv))
    {
        mls_buf_free(&leaf);
        return fail(s, "could not build the commit leaf");
    }
    g->tree.n[me * 2].has_priv = true;
    memcpy(g->tree.n[me * 2].priv, leaf_priv, NSK);

    uint8_t th[NH];
    tree_root_hash(&g->tree, th);
    MlsBuf ctx = { 0 };
    group_context(g, pc->epoch, th, pc->cth, pc->cth_len, &ctx);

    path->len = 0;
    w_put(path, leaf.data, leaf.len);
    mls_buf_free(&leaf);
    MlsBuf nodes = { 0 };
    bool ok = true;
    for (int k = 0; k < n_fdp && ok; k++)
    {
        w_opaque(&nodes, g->tree.n[fdp[k]].enc, NPK);
        uint32_t res[512];
        int n_res = tree_resolution(&g->tree, copath_child(fdp[k], me * 2), exclude, n_exclude, res, 512);
        MlsBuf cts = { 0 };
        for (int j = 0; j < n_res && ok; j++)
            ok = encrypt_with_label(&cts, g->tree.n[res[j]].enc, "UpdatePathNode", ctx.data, ctx.len,
                                    path_secrets[k], NH);
        w_buf(&nodes, &cts);
        mls_buf_free(&cts);
    }
    w_buf(path, &nodes);
    mls_buf_free(&nodes);
    mls_buf_free(&ctx);
    *n_fdp_out = n_fdp;
    return ok ? true : fail(s, "could not encrypt the update path");
}

static bool apply_update_path(MlsSession *s, Group *g, uint32_t sender, Rd *r, const uint32_t *exclude,
                              int n_exclude, const PathContext *pc, uint8_t commit_secret[NH],
                              uint8_t path_secret_out[NH])
{
    LeafView lv;
    if (!parse_leaf(r, &lv) || lv.source != SRC_COMMIT)
        return fail(s, "bad update path leaf");
    if (sender >= g->tree.leaves || g->tree.n[sender * 2].type != NODE_LEAF)
        return fail(s, "update path from an empty leaf");

    uint32_t fdp[MAX_DEPTH], dp[MAX_DEPTH];
    int n_fdp = tree_filtered_path(&g->tree, sender, fdp);
    int n_dp = t_direct_path(sender * 2, g->tree.leaves, dp);
    for (int i = 0; i < n_dp; i++)
        tree_blank(&g->tree, dp[i]);
    if (!node_set_leaf(&g->tree.n[sender * 2], &lv))
        return fail(s, "out of memory");

    Rd nodes = r_sub(r);
    const uint8_t *node_start[MAX_DEPTH];
    int k = 0;
    while (!nodes.err && nodes.p < nodes.end && k < n_fdp)
    {
        node_start[k] = nodes.p;
        Node *p = &g->tree.n[fdp[k]];
        p->type = NODE_PARENT;
        r_fixed(&nodes, p->enc, NPK);
        r_sub(&nodes);
        k++;
    }
    if (nodes.err || nodes.p != nodes.end || k != n_fdp)
        return fail(s, "update path does not match the tree");

    uint8_t leaf_ph[NH];
    tree_set_parent_hashes(&g->tree, sender, fdp, n_fdp, leaf_ph);
    if (lv.ph_len != NH || memcmp(lv.ph, leaf_ph, NH) != 0)
        return fail(s, "parent hash mismatch in update path");

    uint8_t th[NH];
    tree_root_hash(&g->tree, th);
    MlsBuf ctx = { 0 };
    group_context(g, pc->epoch, th, pc->cth, pc->cth_len, &ctx);

    uint32_t me = g->me;
    int lca = 0;
    while (lca < n_fdp && !t_in_subtree(fdp[lca], me * 2))
        lca++;
    if (lca == n_fdp)
    {
        mls_buf_free(&ctx);
        return fail(s, "update path does not cover us");
    }
    uint32_t res[512];
    int n_res = tree_resolution(&g->tree, path_child(fdp[lca], me * 2), exclude, n_exclude, res, 512);
    int pick = -1;
    for (int j = 0; j < n_res && pick < 0; j++)
        if (g->tree.n[res[j]].has_priv && t_in_subtree(res[j], me * 2))
            pick = j;
    if (pick < 0)
    {
        mls_buf_free(&ctx);
        return fail(s, "no key to decrypt the update path");
    }

    Rd nr = rd(node_start[lca], (size_t)(nodes.end - node_start[lca]));
    uint8_t enc[NPK];
    r_fixed(&nr, enc, NPK);
    Rd cts = r_sub(&nr);
    const uint8_t *kem = NULL, *ct = NULL;
    size_t kem_len = 0, ct_len = 0;
    for (int j = 0; j <= pick; j++)
    {
        r_opaque(&cts, &kem, &kem_len);
        r_opaque(&cts, &ct, &ct_len);
    }
    if (cts.err)
    {
        mls_buf_free(&ctx);
        return fail(s, "missing update path ciphertext");
    }
    Node *holder = &g->tree.n[res[pick]];
    MlsBuf ps = { 0 };
    bool ok = decrypt_with_label(holder->priv, holder->enc, "UpdatePathNode", ctx.data, ctx.len,
                                 kem, kem_len, ct, ct_len, &ps);
    mls_buf_free(&ctx);
    if (!ok || ps.len != NH)
    {
        mls_buf_free(&ps);
        return fail(s, "could not decrypt the update path");
    }
    if (path_secret_out)
        memcpy(path_secret_out, ps.data, NH);
    ok = set_path_keys(&g->tree, fdp, lca, n_fdp, ps.data, commit_secret);
    mls_buf_free(&ps);
    return ok ? true : fail(s, "update path keys do not match the path secret");
}

static bool create_commit(MlsSession *s, const Group *base, MlsBuf *out)
{
    Proposal *props[MAX_QUEUE];
    for (int i = 0; i < s->n_queue; i++)
        props[i] = &s->queue[i];

    Group *ng = group_copy(base);
    if (!ng)
        return fail(s, "out of memory");
    Added added[MAX_QUEUE];
    int n_added;
    bool has_remove;
    if (!apply_proposals(s, ng, props, s->n_queue, added, &n_added, &has_remove))
    {
        group_free(ng);
        return false;
    }

    uint32_t exclude[MAX_QUEUE];
    for (int i = 0; i < n_added; i++)
        exclude[i] = added[i].leaf;

    bool with_path = has_remove || s->n_queue == 0;
    uint8_t commit_secret[NH];
    memset(commit_secret, 0, NH);
    MlsBuf path = { 0 };
    uint32_t fdp[MAX_DEPTH];
    int n_fdp = 0;
    uint8_t path_secrets[MAX_DEPTH + 1][NH];

    PathContext pc = { base->epoch + 1, base->cth, base->cth_len };
    if (with_path && !create_update_path(s, ng, exclude, n_added, &pc, &path, path_secrets, fdp, &n_fdp,
                                         commit_secret))
    {
        mls_buf_free(&path);
        group_free(ng);
        return false;
    }

    MlsBuf old_ctx = { 0 };
    group_current_context(base, &old_ctx);

    MlsBuf content = { 0 };
    w_opaque(&content, base->gid, base->gid_len);
    w_u64(&content, base->epoch);
    w_u8(&content, SENDER_MEMBER);
    w_u32(&content, base->me);
    w_varint(&content, 0);
    w_u8(&content, CT_COMMIT);
    MlsBuf refs = { 0 };
    for (int i = 0; i < s->n_queue; i++)
    {
        w_u8(&refs, 2);
        w_opaque(&refs, s->queue[i].ref, NH);
    }
    w_buf(&content, &refs);
    mls_buf_free(&refs);
    w_u8(&content, with_path ? 1 : 0);
    w_put(&content, path.data, path.len);
    mls_buf_free(&path);

    MlsBuf tbs = { 0 };
    w_u16(&tbs, 1);
    w_u16(&tbs, WIRE_PUBLIC);
    w_put(&tbs, content.data, content.len);
    w_put(&tbs, old_ctx.data, old_ctx.len);
    mls_buf_free(&old_ctx);
    uint8_t sig[80];
    size_t sig_len = sign_with_label(s->sig_priv, "FramedContentTBS", tbs.data, tbs.len, sig);

    uint8_t joiner[NH], welcome_secret[NH], tag[NH];
    finish_epoch(s, ng, base, content.data, content.len, sig, sig_len, commit_secret, joiner, welcome_secret, tag);

    MlsBuf tbm = { 0 };
    w_put(&tbm, tbs.data, tbs.len);
    w_opaque(&tbm, sig, sig_len);
    w_opaque(&tbm, tag, NH);
    uint8_t mtag[NH];
    hmac(base->membership_key, NH, tbm.data, tbm.len, mtag);
    mls_buf_free(&tbm);
    mls_buf_free(&tbs);

    MlsBuf msg = { 0 };
    w_u16(&msg, 1);
    w_u16(&msg, WIRE_PUBLIC);
    w_put(&msg, content.data, content.len);
    w_opaque(&msg, sig, sig_len);
    w_opaque(&msg, tag, NH);
    w_opaque(&msg, mtag, NH);
    mls_buf_free(&content);

    out->len = 0;
    w_put(out, msg.data, msg.len);

    if (n_added > 0)
    {
        MlsBuf gi = { 0 };
        group_current_context(ng, &gi);
        MlsBuf ext = { 0 };
        w_u16(&ext, EXT_RATCHET_TREE);
        MlsBuf tree_data = { 0 };
        tree_serialize(&ng->tree, &tree_data);
        w_buf(&ext, &tree_data);
        mls_buf_free(&tree_data);
        w_buf(&gi, &ext);
        mls_buf_free(&ext);
        w_opaque(&gi, tag, NH);
        w_u32(&gi, ng->me);
        uint8_t gsig[80];
        size_t gsig_len = sign_with_label(s->sig_priv, "GroupInfoTBS", gi.data, gi.len, gsig);
        w_opaque(&gi, gsig, gsig_len);

        uint8_t wkey[NK], wnonce[NN], gtag[16];
        mls_expand_with_label(welcome_secret, NH, "key", NULL, 0, wkey, NK);
        mls_expand_with_label(welcome_secret, NH, "nonce", NULL, 0, wnonce, NN);
        mls_aes_gcm(true, wkey, NK, wnonce, NN, NULL, 0, gi.data, gi.len, gtag);
        w_put(&gi, gtag, 16);

        MlsBuf secrets = { 0 };
        for (int i = 0; i < n_added; i++)
        {
            MlsBuf gs = { 0 };
            w_opaque(&gs, joiner, NH);
            if (with_path)
            {
                int k = 0;
                while (k < n_fdp && !t_in_subtree(fdp[k], added[i].leaf * 2))
                    k++;
                w_u8(&gs, 1);
                w_opaque(&gs, path_secrets[k], NH);
            }
            else
            {
                w_u8(&gs, 0);
            }
            w_varint(&gs, 0);
            w_opaque(&secrets, added[i].kp_ref, NH);
            encrypt_with_label(&secrets, added[i].init_key, "Welcome", gi.data, gi.len, gs.data, gs.len);
            mls_buf_free(&gs);
        }

        w_u16(out, SUITE);
        w_buf(out, &secrets);
        w_buf(out, &gi);
        mls_buf_free(&secrets);
        mls_buf_free(&gi);
    }

    s->out_commit.len = 0;
    w_put(&s->out_commit, msg.data, msg.len);
    mls_buf_free(&msg);
    group_free(s->out_state);
    s->out_state = ng;
    return true;
}

static bool parse_proposal_message(Rd *list, PublicMessage *m)
{
    Rd t = *list;
    r_u16(&t);
    r_u16(&t);
    r_sub(&t);
    r_u64(&t);
    uint8_t st = r_u8(&t);
    if (st == SENDER_MEMBER || st == SENDER_EXTERNAL)
        r_u32(&t);
    r_sub(&t);
    uint16_t type;
    if (r_u8(&t) != CT_PROPOSAL || !skip_proposal(&t, &type))
        return false;
    r_sub(&t);
    if (st == SENDER_MEMBER)
        r_sub(&t);
    if (t.err)
        return false;
    size_t used = (size_t)(t.p - list->p);
    if (!parse_public_message(list->p, used, m))
        return false;
    list->p += used;
    return true;
}

int mls_process_proposals(MlsSession *s, const uint8_t *data, size_t len,
                          const uint64_t *users, int n_users, MlsBuf *out)
{
    Group *base = s->pending ? s->pending : s->group;
    if (!base)
    {
        fail(s, "proposals arrived without a group");
        return -1;
    }
    if (len < 1)
    {
        fail(s, "empty proposals message");
        return -1;
    }

    uint8_t op = data[0];
    Rd r = rd(data + 1, len - 1);
    Rd list = r_sub(&r);
    if (list.err || !r_done(&r))
    {
        fail(s, "malformed proposals message");
        return -1;
    }

    if (op == 0)
    {
        while (list.p < list.end)
        {
            PublicMessage m;
            if (!parse_proposal_message(&list, &m))
            {
                fail(s, "malformed proposal");
                return -1;
            }
            if (!check_external_proposal(s, base, &m) || !queue_proposal(s, &m, users, n_users))
                return -1;
        }
    }
    else if (op == 1)
    {
        while (list.p < list.end)
        {
            const uint8_t *ref;
            size_t ref_len;
            if (!r_opaque(&list, &ref, &ref_len) || ref_len != NH)
            {
                fail(s, "malformed proposal ref");
                return -1;
            }
            int found = -1;
            for (int i = 0; i < s->n_queue; i++)
                if (memcmp(s->queue[i].ref, ref, NH) == 0)
                    found = i;
            if (found < 0)
            {
                fail(s, "revoked an unknown proposal");
                return -1;
            }
            free(s->queue[found].kp);
            memmove(&s->queue[found], &s->queue[found + 1], (size_t)(s->n_queue - found - 1) * sizeof(Proposal));
            s->n_queue--;
        }
    }
    else
    {
        fail(s, "unknown proposals operation");
        return -1;
    }

    if (s->n_queue == 0)
    {
        group_free(s->out_state);
        s->out_state = NULL;
        mls_buf_free(&s->out_commit);
        return 0;
    }
    return create_commit(s, base, out) ? 1 : -1;
}

static void adopt(MlsSession *s, Group *g)
{
    if (s->out_state == g)
        s->out_state = NULL;
    group_free(s->group);
    s->group = g;
    clear_pending(s);
}

static bool handle_commit(MlsSession *s, const PublicMessage *m)
{
    Group *base = s->group;
    if (m->sender_type != SENDER_MEMBER || m->content_type != CT_COMMIT)
        return fail(s, "not a member commit");
    if (m->epoch != base->epoch)
        return fail(s, "commit epoch mismatch");
    if (m->gid_len != base->gid_len || memcmp(m->gid, base->gid, m->gid_len) != 0)
        return fail(s, "commit for another group");
    if (m->sender >= base->tree.leaves || base->tree.n[m->sender * 2].type != NODE_LEAF)
        return fail(s, "commit from an unknown member");

    MlsBuf old_ctx = { 0 };
    group_current_context(base, &old_ctx);
    MlsBuf tbs = { 0 };
    w_content_tbs(&tbs, m, &old_ctx);
    mls_buf_free(&old_ctx);
    bool sig_ok = verify_with_label(base->tree.n[m->sender * 2].sig, NPK, "FramedContentTBS",
                                    tbs.data, tbs.len, m->sig, m->sig_len);
    w_auth(&tbs, m);
    uint8_t mtag[NH];
    hmac(base->membership_key, NH, tbs.data, tbs.len, mtag);
    mls_buf_free(&tbs);
    if (!sig_ok)
        return fail(s, "bad commit signature");
    if (m->mtag_len != NH || memcmp(mtag, m->mtag, NH) != 0)
        return fail(s, "bad membership tag");

    Rd r = rd(m->body, m->body_len);
    Rd list = r_sub(&r);
    Proposal *props[MAX_QUEUE];
    int n_props = 0;
    while (!list.err && list.p < list.end)
    {
        if (r_u8(&list) != 2)
            return fail(s, "commit has an inline proposal");
        const uint8_t *ref;
        size_t ref_len;
        if (!r_opaque(&list, &ref, &ref_len) || ref_len != NH || n_props >= MAX_QUEUE)
            return fail(s, "malformed proposal ref in commit");
        Proposal *found = NULL;
        for (int i = 0; i < s->n_queue; i++)
            if (memcmp(s->queue[i].ref, ref, NH) == 0)
                found = &s->queue[i];
        if (!found)
            return fail(s, "commit references an unknown proposal");
        props[n_props++] = found;
    }
    if (list.err)
        return fail(s, "malformed commit");

    Group *ng = group_copy(base);
    if (!ng)
        return fail(s, "out of memory");
    Added added[MAX_QUEUE];
    int n_added;
    bool has_remove;
    if (!apply_proposals(s, ng, props, n_props, added, &n_added, &has_remove))
    {
        group_free(ng);
        return false;
    }

    uint8_t commit_secret[NH];
    memset(commit_secret, 0, NH);
    uint8_t has_path = r_u8(&r);
    if (has_path == 1)
    {
        uint32_t exclude[MAX_QUEUE];
        for (int i = 0; i < n_added; i++)
            exclude[i] = added[i].leaf;
        PathContext pc = { base->epoch + 1, base->cth, base->cth_len };
        if (!apply_update_path(s, ng, m->sender, &r, exclude, n_added, &pc, commit_secret, NULL))
        {
            group_free(ng);
            return false;
        }
    }
    if (has_path > 1 || !r_done(&r) || (has_remove && has_path != 1))
    {
        group_free(ng);
        return fail(s, "malformed commit");
    }

    uint8_t joiner[NH], welcome_secret[NH], tag[NH];
    finish_epoch(s, ng, base, m->content, m->content_len, m->sig, m->sig_len, commit_secret, joiner,
                 welcome_secret, tag);
    if (m->tag_len != NH || memcmp(tag, m->tag, NH) != 0)
    {
        group_free(ng);
        return fail(s, "confirmation tag mismatch");
    }
    adopt(s, ng);
    return true;
}

int mls_process_commit(MlsSession *s, const uint8_t *data, size_t len)
{
    if (!s->group && !s->pending)
        return 0;
    if (s->out_state && s->out_commit.len == len && memcmp(s->out_commit.data, data, len) == 0)
    {
        adopt(s, s->out_state);
        return 1;
    }
    if (!s->group)
        return 0;
    if (s->n_queue == 0)
    {
        fail(s, "commit without queued proposals");
        return -1;
    }
    PublicMessage m;
    if (!parse_public_message(data, len, &m))
    {
        fail(s, "malformed commit");
        return -1;
    }
    return handle_commit(s, &m) ? 1 : -1;
}

static bool check_welcome_roster(MlsSession *s, const Group *g, const uint64_t *users, int n_users)
{
    for (uint32_t l = 0; l < g->tree.leaves; l++)
    {
        const Node *leaf = &g->tree.n[l * 2];
        if (leaf->type != NODE_LEAF)
            continue;
        if (users && !user_recognized(leaf->uid, users, n_users))
            return fail(s, "welcome lists an unrecognized user");
        for (uint32_t o = 0; o < l; o++)
            if (g->tree.n[o * 2].type == NODE_LEAF && g->tree.n[o * 2].uid == leaf->uid)
                return fail(s, "welcome lists a user twice");
    }

    const uint8_t *es;
    size_t es_len;
    if (!group_external_sender(g, &es, &es_len))
        return fail(s, "welcomed group has no external sender");
    Rd er = rd(es, es_len);
    const uint8_t *one = er.p;
    r_sub(&er);
    r_u16(&er);
    r_sub(&er);
    if (er.err || !r_done(&er) || (size_t)(er.p - one) != s->es.len || memcmp(one, s->es.data, s->es.len) != 0)
        return fail(s, "welcomed group has a different external sender");
    return true;
}

static bool join_welcome(MlsSession *s, const uint8_t *data, size_t len, const uint8_t *ext_tree,
                         size_t ext_tree_len, Group **out)
{
    Rd r = rd(data, len);
    if (r_u16(&r) != SUITE)
        return fail(s, "welcome for another cipher suite");
    Rd secrets = r_sub(&r);
    const uint8_t *egi;
    size_t egi_len;
    if (!r_opaque(&r, &egi, &egi_len) || !r_done(&r) || egi_len < 16)
        return fail(s, "malformed welcome");

    const uint8_t *kem = NULL, *ct = NULL;
    size_t kem_len = 0, ct_len = 0;
    bool found = false;
    while (!secrets.err && secrets.p < secrets.end && !found)
    {
        const uint8_t *ref;
        size_t ref_len;
        r_opaque(&secrets, &ref, &ref_len);
        r_opaque(&secrets, &kem, &kem_len);
        r_opaque(&secrets, &ct, &ct_len);
        if (!secrets.err && ref_len == NH && memcmp(ref, s->kp_ref, NH) == 0)
            found = true;
    }
    if (!found)
        return fail(s, "welcome is not for our key package");

    MlsBuf gs = { 0 };
    if (!decrypt_with_label(s->init_priv, s->init_pub, "Welcome", egi, egi_len, kem, kem_len, ct, ct_len, &gs))
        return fail(s, "could not decrypt the group secrets");
    Rd gr = rd(gs.data, gs.len);
    uint8_t joiner[NH], path_secret[NH];
    bool have_ps = false;
    r_fixed(&gr, joiner, NH);
    uint8_t opt = r_u8(&gr);
    if (opt == 1)
        have_ps = r_fixed(&gr, path_secret, NH);
    Rd psks = r_sub(&gr);
    bool gs_ok = !gr.err && opt <= 1 && psks.p == psks.end && r_done(&gr);
    mls_buf_free(&gs);
    if (!gs_ok)
        return fail(s, "malformed group secrets");

    uint8_t member[NH], welcome_secret[NH], wkey[NK], wnonce[NN];
    hkdf_extract(joiner, NH, ZEROS, NH, member);
    derive_secret(member, "welcome", welcome_secret);
    mls_expand_with_label(welcome_secret, NH, "key", NULL, 0, wkey, NK);
    mls_expand_with_label(welcome_secret, NH, "nonce", NULL, 0, wnonce, NN);
    size_t gi_len = egi_len - 16;
    uint8_t *gi = malloc(gi_len ? gi_len : 1);
    if (!gi)
        return fail(s, "out of memory");
    memcpy(gi, egi, gi_len);
    if (!mls_aes_gcm_check(wkey, NK, wnonce, NN, NULL, 0, gi, gi_len, egi + gi_len, 16))
    {
        free(gi);
        return fail(s, "could not decrypt the group info");
    }

    Group *g = calloc(1, sizeof(Group));
    MlsBuf ctx = { 0 };
    bool ok = false;
    if (!g)
    {
        fail(s, "out of memory");
        goto done;
    }

    Rd ir = rd(gi, gi_len);
    const uint8_t *ctx_start = ir.p, *p;
    size_t n;
    uint8_t th[NH], tag[NH];
    if (r_u16(&ir) != 1 || r_u16(&ir) != SUITE)
    {
        fail(s, "group info has the wrong version");
        goto done;
    }
    if (!r_opaque(&ir, &p, &n) || n > sizeof(g->gid))
    {
        fail(s, "malformed group info");
        goto done;
    }
    memcpy(g->gid, p, n);
    g->gid_len = n;
    g->epoch = r_u64(&ir);
    r_fixed(&ir, th, NH);
    if (!r_opaque(&ir, &p, &n) || n > NH)
    {
        fail(s, "malformed group info");
        goto done;
    }
    memcpy(g->cth, p, n);
    g->cth_len = n;
    const uint8_t *ext_start = ir.p;
    r_sub(&ir);
    if (ir.err)
    {
        fail(s, "malformed group info");
        goto done;
    }
    w_put(&g->ext, ext_start, (size_t)(ir.p - ext_start));
    w_put(&ctx, ctx_start, (size_t)(ir.p - ctx_start));

    bool have_tree = false;
    Rd exts = r_sub(&ir);
    while (!exts.err && exts.p < exts.end)
    {
        uint16_t type = r_u16(&exts);
        Rd d = r_sub(&exts);
        if (type == EXT_RATCHET_TREE && !have_tree && !ext_tree)
        {
            have_tree = tree_parse(&d, &g->tree) && r_done(&d);
            if (!have_tree)
                break;
        }
    }
    if (ext_tree && !have_tree)
    {
        Rd tr = rd(ext_tree, ext_tree_len);
        have_tree = tree_parse(&tr, &g->tree) && r_done(&tr);
    }
    r_fixed(&ir, tag, NH);
    uint32_t signer = r_u32(&ir);
    size_t tbs_len = (size_t)(ir.p - gi);
    const uint8_t *gsig;
    size_t gsig_len;
    r_opaque(&ir, &gsig, &gsig_len);
    if (exts.err || !r_done(&ir) || !have_tree)
    {
        fail(s, have_tree ? "malformed group info" : "group info has no ratchet tree");
        goto done;
    }

    uint8_t real_th[NH];
    tree_root_hash(&g->tree, real_th);
    if (memcmp(real_th, th, NH) != 0)
    {
        fail(s, "ratchet tree does not match the tree hash");
        goto done;
    }
    if (signer >= g->tree.leaves || g->tree.n[signer * 2].type != NODE_LEAF ||
        !verify_with_label(g->tree.n[signer * 2].sig, NPK, "GroupInfoTBS", gi, tbs_len, gsig, gsig_len))
    {
        fail(s, "bad group info signature");
        goto done;
    }

    g->me = UINT32_MAX;
    for (uint32_t l = 0; l < g->tree.leaves; l++)
    {
        Node *leaf = &g->tree.n[l * 2];
        if (leaf->type == NODE_LEAF && memcmp(leaf->enc, s->leaf_pub, NPK) == 0 &&
            memcmp(leaf->sig, s->sig_pub, NPK) == 0)
            g->me = l;
    }
    if (g->me == UINT32_MAX)
    {
        fail(s, "we are not in the welcomed group");
        goto done;
    }
    g->tree.n[g->me * 2].has_priv = true;
    memcpy(g->tree.n[g->me * 2].priv, s->leaf_priv, NSK);

    group_from_joiner(g, joiner, &ctx, NULL);
    uint8_t want_tag[NH];
    hmac(g->confirm_key, NH, g->cth, g->cth_len, want_tag);
    if (memcmp(want_tag, tag, NH) != 0)
    {
        fail(s, "confirmation tag mismatch in welcome");
        goto done;
    }
    group_update_interim(g, tag);

    if (have_ps)
    {
        uint32_t fdp[MAX_DEPTH];
        int n_fdp = tree_filtered_path(&g->tree, signer, fdp);
        int lca = 0;
        while (lca < n_fdp && !t_in_subtree(fdp[lca], g->me * 2))
            lca++;
        if (lca == n_fdp || !set_path_keys(&g->tree, fdp, lca, n_fdp, path_secret, NULL))
        {
            fail(s, "path secret in welcome does not match the tree");
            goto done;
        }
    }
    ok = true;

done:
    mls_buf_free(&ctx);
    free(gi);
    if (!ok)
    {
        group_free(g);
        return false;
    }
    *out = g;
    return true;
}

bool mls_process_welcome(MlsSession *s, const uint8_t *data, size_t len, const uint64_t *users, int n_users)
{
    if (s->group)
        return fail(s, "already in a group");
    if (!s->have_kp || !s->have_leaf)
        return fail(s, "welcome without a key package");
    if (!s->es.len)
        return fail(s, "welcome without an external sender");
    Group *g = NULL;
    if (!join_welcome(s, data, len, NULL, 0, &g))
        return false;
    if (!check_welcome_roster(s, g, users, n_users))
    {
        group_free(g);
        return false;
    }
    adopt(s, g);
    return true;
}
