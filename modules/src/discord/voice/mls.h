#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *data;
    size_t   len;
    size_t   cap;
}
MlsBuf;

void mls_buf_free(MlsBuf *b);

typedef struct MlsSession MlsSession;

MlsSession *mls_session_new(uint64_t user_id, uint64_t group_id);
void        mls_session_free(MlsSession *s);
void        mls_session_init(MlsSession *s);

bool mls_set_external_sender(MlsSession *s, const uint8_t *data, size_t len);
bool mls_key_package(MlsSession *s, MlsBuf *out);

int  mls_process_proposals(MlsSession *s, const uint8_t *data, size_t len,
                           const uint64_t *users, int n_users, MlsBuf *out);
int  mls_process_commit(MlsSession *s, const uint8_t *data, size_t len);
bool mls_process_welcome(MlsSession *s, const uint8_t *data, size_t len,
                         const uint64_t *users, int n_users);

bool     mls_established(const MlsSession *s);
uint64_t mls_epoch(const MlsSession *s);
int      mls_roster(const MlsSession *s, uint64_t *out, int max);
bool     mls_export(const MlsSession *s, const char *label, const uint8_t *ctx, size_t ctx_len,
                    uint8_t *out, size_t len);
bool     mls_epoch_authenticator(const MlsSession *s, uint8_t out[32]);
const char *mls_error(const MlsSession *s);

void mls_expand_with_label(const uint8_t *secret, size_t secret_len, const char *label,
                           const uint8_t *ctx, size_t ctx_len, uint8_t *out, uint16_t len);
void mls_aes_gcm(bool encrypt, const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len,
                 const uint8_t *aad, size_t aad_len, uint8_t *data, size_t len, uint8_t tag[16]);
bool mls_aes_gcm_check(const uint8_t *key, size_t key_len, const uint8_t *iv, size_t iv_len,
                       const uint8_t *aad, size_t aad_len, uint8_t *data, size_t len,
                       const uint8_t *tag, size_t tag_len);

void random_bytes(void *out, size_t len);
