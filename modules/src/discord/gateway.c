#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discord.h"

char api_base[256]    = "https://discord.com/api/v9";
char gateway_url[256] = "wss://gateway.discord.gg/?v=9&encoding=json";
char cdn_base[256]    = "https://cdn.discordapp.com";
char auth_token[256];

#define INTENTS_BOT ((1 << 0) | (1 << 9) | (1 << 12) | (1 << 15))

static Ws      *ws;
static GwState  state = GW_OFF;
static char     error_text[192];
static uint64_t hb_interval;
static uint64_t next_hb;
static bool     hb_acked;
static int64_t  seq = -1;
static char     session_id[128];
static char     resume_url[256];
static int      attempts;
static uint64_t reconnect_at;

static bool is_bot(void)
{
    return strncmp(auth_token, "Bot ", 4) == 0;
}

const char *api_headers(void)
{
    static char headers[512];
    snprintf(headers, sizeof(headers),
             "Authorization: %s\r\nContent-Type: application/json\r\nX-Discord-Locale: en-US\r\n"
             "Origin: https://discord.com\r\n",
             auth_token);
    return headers;
}

bool api_request(const char *method, const char *path, const char *json, HttpDone done, void *user)
{
    char url[768];
    snprintf(url, sizeof(url), "%s%s", api_base, path);
    return http_request(method, url, api_headers(), json, json ? strlen(json) : 0, done, user);
}

static void connect_to(const char *url)
{
    ws_free(ws);
    ws = ws_open(url, "https://discord.com");
    hb_interval = 0;
    next_hb = 0;
    hb_acked = true;
}

static void schedule_reconnect(void)
{
    ws_free(ws);
    ws = NULL;
    attempts++;
    uint64_t delay = attempts <= 1 ? 500 : 1000ULL << (attempts > 5 ? 5 : attempts - 1);
    reconnect_at = uptime_ms() + delay;
}

static void fail(const char *msg)
{
    ws_free(ws);
    ws = NULL;
    state = GW_FAILED;
    reconnect_at = 0;
    snprintf(error_text, sizeof(error_text), "%s", msg);
}

void gw_start(const char *token)
{
    snprintf(auth_token, sizeof(auth_token), "%s", token);
    seq = -1;
    session_id[0] = '\0';
    resume_url[0] = '\0';
    attempts = 0;
    reconnect_at = 0;
    error_text[0] = '\0';
    state = GW_CONNECTING;
    connect_to(gateway_url);
}

void gw_stop(void)
{
    ws_free(ws);
    ws = NULL;
    state = GW_OFF;
    reconnect_at = 0;
    session_id[0] = '\0';
    seq = -1;
}

GwState gw_state(void)
{
    if (state == GW_CONNECTING && S.ready)
        return GW_READY;
    return state;
}

const char *gw_error(void)
{
    return error_text;
}

static void send_heartbeat(void)
{
    char msg[64];
    if (seq >= 0)
        snprintf(msg, sizeof(msg), "{\"op\":1,\"d\":%lld}", (long long)seq);
    else
        snprintf(msg, sizeof(msg), "{\"op\":1,\"d\":null}");
    ws_send(ws, msg, strlen(msg));
    hb_acked = false;
}

static void send_identify(void)
{
    Buf b = { 0 };
    const char *token = is_bot() ? auth_token + 4 : auth_token;
    if (session_id[0] && seq >= 0)
    {
        buf_puts(&b, "{\"op\":6,\"d\":{\"token\":");
        buf_json_string(&b, token);
        buf_puts(&b, ",\"session_id\":");
        buf_json_string(&b, session_id);
        buf_printf(&b, ",\"seq\":%lld}}", (long long)seq);
    }
    else
    {
        buf_puts(&b, "{\"op\":2,\"d\":{\"token\":");
        buf_json_string(&b, token);
        if (is_bot())
        {
            buf_printf(&b, ",\"intents\":%d,\"properties\":{\"os\":\"strawos\",\"browser\":\"strawcord\",\"device\":\"strawcord\"}", INTENTS_BOT);
        }
        else
        {
            buf_puts(&b, ",\"capabilities\":0,\"properties\":{\"os\":\"Linux\",\"browser\":\"Chrome\",\"device\":\"\","
                         "\"system_locale\":\"en-US\",\"browser_user_agent\":\"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36\",\"browser_version\":\"126.0.0.0\",\"os_version\":\"\","
                         "\"referrer\":\"\",\"referring_domain\":\"\",\"release_channel\":\"stable\"},"
                         "\"presence\":{\"status\":\"online\",\"since\":0,\"activities\":[],\"afk\":false},"
                         "\"compress\":false,\"client_state\":{\"guild_versions\":{}}");
        }
        buf_puts(&b, "}}");
    }
    ws_send(ws, b.data, b.len);
    buf_free(&b);
}

static void dispatch(const char *t, Json *d)
{
    if (strcmp(t, "READY") == 0)
    {
        if (S.ready)
            store_reset();
        snprintf(session_id, sizeof(session_id), "%s", json_str(d, "session_id") ? json_str(d, "session_id") : "");
        const char *ru = json_str(d, "resume_gateway_url");
        if (ru)
            snprintf(resume_url, sizeof(resume_url), "%s/?v=9&encoding=json", ru);
        store_on_ready(d);
        state = GW_READY;
        attempts = 0;
    }
    else if (strcmp(t, "RESUMED") == 0)
    {
        state = GW_READY;
        attempts = 0;
    }
    else if (strcmp(t, "GUILD_CREATE") == 0)
        store_on_guild_create(d);
    else if (strcmp(t, "GUILD_DELETE") == 0)
        store_on_guild_delete(d);
    else if (strcmp(t, "CHANNEL_CREATE") == 0 || strcmp(t, "CHANNEL_UPDATE") == 0)
        store_on_channel(d, false);
    else if (strcmp(t, "CHANNEL_DELETE") == 0)
        store_on_channel(d, true);
    else if (strcmp(t, "MESSAGE_CREATE") == 0)
        store_on_message_create(d);
    else if (strcmp(t, "MESSAGE_UPDATE") == 0)
        store_on_message_update(d);
    else if (strcmp(t, "MESSAGE_DELETE") == 0)
        store_on_message_delete(d);
}

static void handle(const char *text, size_t len)
{
    JsonDoc *doc = json_parse(text, len);
    Json *root = json_root(doc);
    if (!root)
    {
        json_free(doc);
        return;
    }
    int op = (int)json_int(root, "op", -1);
    Json *d = json_get(root, "d");
    switch (op)
    {
        case 0:
        {
            Json *s = json_get(root, "s");
            if (s && s->type == JSON_NUMBER)
                seq = (int64_t)s->num;
            const char *t = json_str(root, "t");
            if (t && d)
                dispatch(t, d);
            break;
        }
        case 1:
            send_heartbeat();
            break;
        case 7:
            json_free(doc);
            connect_to(resume_url[0] ? resume_url : gateway_url);
            return;
        case 9:
            if (!(d && d->type == JSON_TRUE))
            {
                session_id[0] = '\0';
                seq = -1;
            }
            json_free(doc);
            schedule_reconnect();
            reconnect_at = uptime_ms() + 1500;
            return;
        case 10:
        {
            hb_interval = (uint64_t)json_int(d, "heartbeat_interval", 41250);
            uint8_t r;
            random_bytes(&r, 1);
            next_hb = uptime_ms() + hb_interval * r / 255;
            hb_acked = true;
            send_identify();
            break;
        }
        case 11:
            hb_acked = true;
            break;
        default:
            break;
    }
    json_free(doc);
}

static void on_closed(void)
{
    int code = ws_close_code(ws);
    switch (code)
    {
        case 4004:
            fail("Discord rejected the token. Log in again.");
            return;
        case 4010:
        case 4011:
        case 4012:
        case 4013:
            fail("Discord refused the connection settings for this account.");
            return;
        case 4014:
            fail("This bot needs the Message Content and Server Members intents enabled in the Developer Portal.");
            return;
        case 4007:
        case 4009:
            session_id[0] = '\0';
            seq = -1;
            break;
        default:
            break;
    }
    if (!S.ready && attempts >= 4)
    {
        char msg[192];
        snprintf(msg, sizeof(msg), "Couldn't connect to Discord: %s", ws_error(ws));
        fail(msg);
        return;
    }
    schedule_reconnect();
}

void gw_step(void)
{
    if (state == GW_OFF || state == GW_FAILED)
        return;
    uint64_t now = uptime_ms();
    if (!ws)
    {
        if (reconnect_at && now >= reconnect_at)
        {
            reconnect_at = 0;
            connect_to(session_id[0] && resume_url[0] ? resume_url : gateway_url);
        }
        return;
    }
    WsState st = ws_step(ws);
    char *msg;
    size_t len;
    while (ws && ws_recv(ws, &msg, &len))
    {
        handle(msg, len);
        free(msg);
        if (state == GW_FAILED || state == GW_OFF)
            return;
    }
    if (!ws)
        return;
    if (st == WS_CLOSED)
    {
        on_closed();
        return;
    }
    if (st == WS_OPEN && hb_interval && now >= next_hb)
    {
        if (!hb_acked)
        {
            schedule_reconnect();
            reconnect_at = now;
            return;
        }
        send_heartbeat();
        next_hb = now + hb_interval;
    }
}

int gw_pollfds(struct pollfd *fds, int max)
{
    if (max < 1 || !ws)
        return 0;
    return ws_pollfd(ws, fds);
}

bool gw_has_buffered(void)
{
    return ws_has_buffered(ws);
}

uint64_t gw_next_deadline(void)
{
    if (!ws)
        return reconnect_at;
    return hb_interval ? next_hb : 0;
}
