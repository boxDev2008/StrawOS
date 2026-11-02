#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAVE_MAX_PROTOCOL 1
#define DAVE_FRAME_OVERHEAD 24

typedef struct Dave Dave;

typedef struct
{
    void *user;
    void (*send_json)(void *user, const char *json);
    void (*send_binary)(void *user, int op, const uint8_t *data, size_t len);
    void (*log)(void *user, const char *msg);
}
DaveIo;

Dave *dave_new(uint64_t user_id, uint64_t channel_id, const DaveIo *io);
void  dave_free(Dave *d);

void dave_select_protocol(Dave *d, int version);
void dave_prepare_transition(Dave *d, uint16_t transition_id, int version);
void dave_execute_transition(Dave *d, uint16_t transition_id);
void dave_prepare_epoch(Dave *d, uint64_t epoch, int version);
void dave_binary(Dave *d, int op, const uint8_t *payload, size_t len);
void dave_clients_connect(Dave *d, const uint64_t *ids, int n);
void dave_client_disconnect(Dave *d, uint64_t id);
void dave_tick(Dave *d, uint64_t now_ms);

int  dave_protocol(const Dave *d);
bool dave_ready(const Dave *d);
uint64_t dave_epoch(const Dave *d);

bool dave_encrypt(Dave *d, const uint8_t *frame, size_t len, uint8_t *out, size_t *out_len);
bool dave_encrypt_video(Dave *d, const uint8_t *frame, size_t len, size_t clear, uint8_t *out, size_t *out_len);
bool dave_decrypt(Dave *d, uint64_t sender, const uint8_t *frame, size_t len, uint8_t *out, size_t *out_len);
