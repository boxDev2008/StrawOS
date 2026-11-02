#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <socket.h>

#include "discord.h"

#define HELPER         "/bin/discord-voice.elf"
#define JOIN_TIMEOUT   15000
#define STREAM_TIMEOUT 20000
#define MAX_SPEAKING   64

typedef struct
{
    int    listener;
    int    conn;
    char   ctl_name[48];
    pid_t  pid;
    char   in[1024];
    size_t in_len;
}
Helper;

static struct
{
    VcState   state;
    Snowflake guild;
    Snowflake channel;
    bool      muted;
    bool      deafened;

    char      session[128];
    char      token[192];
    char      endpoint[256];

    Helper    voice;
    int       spawns;

    Snowflake speaking[MAX_SPEAKING];
    int       n_speaking;
    bool      encrypted;
    char      error[160];
    bool      changed;
    uint64_t  join_at;

    StreamState st_state;
    char      st_key[128];
    char      st_server[32];
    char      st_token[192];
    char      st_endpoint[256];
    Helper    st;
    int       st_viewers;
    char      st_error[160];
    uint64_t  st_at;
}
VC = { .voice = { .listener = -1, .conn = -1 }, .st = { .listener = -1, .conn = -1 } };

static void changed(void)
{
    VC.changed = true;
}

static void send_voice_state(Snowflake guild, Snowflake channel)
{
    char json[256];
    char g[32] = "null", c[32] = "null";
    if (guild)
        snprintf(g, sizeof(g), "\"%llu\"", (unsigned long long)guild);
    if (channel)
        snprintf(c, sizeof(c), "\"%llu\"", (unsigned long long)channel);
    snprintf(json, sizeof(json),
             "{\"op\":4,\"d\":{\"guild_id\":%s,\"channel_id\":%s,\"self_mute\":%s,\"self_deaf\":%s}}",
             g, c, VC.muted ? "true" : "false", VC.deafened ? "true" : "false");
    gw_send(json);
}

static void helper_write(Helper *h, const char *line)
{
    if (h->conn >= 0)
        write(h->conn, line, strlen(line));
}

static void helper_stop(Helper *h)
{
    if (h->conn >= 0)
    {
        helper_write(h, "quit\n");
        close(h->conn);
    }
    if (h->listener >= 0)
        close(h->listener);
    if (h->pid > 0)
        kill(h->pid);
    h->conn = h->listener = -1;
    h->pid = 0;
    h->in_len = 0;
}

static bool helper_start(Helper *h, const char **argv)
{
    helper_stop(h);
    snprintf(h->ctl_name, sizeof(h->ctl_name), "discord-voice-%d-%d", (int)getpid(), ++VC.spawns);
    h->listener = socket(SOCK_NONBLOCK);
    if (h->listener < 0 || bind(h->listener, h->ctl_name) != 0 || listen(h->listener) != 0)
        return false;
    for (int i = 0; argv[i]; i++)
        if (strcmp(argv[i], "--ctl") == 0 && argv[i + 1])
            argv[i + 1] = h->ctl_name;
    h->pid = spawn(HELPER, argv);
    return h->pid != 0;
}

static bool helper_step(Helper *h, void (*on_line)(char *line))
{
    if (h->listener >= 0 && h->conn < 0)
    {
        int c = accept(h->listener);
        if (c >= 0)
            h->conn = c;
    }
    if (h->conn < 0)
        return true;
    for (;;)
    {
        char buf[256];
        ssize_t n = read(h->conn, buf, sizeof(buf));
        if (n == 0)
            return false;
        if (n < 0)
            return true;
        for (ssize_t i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                h->in[h->in_len] = '\0';
                h->in_len = 0;
                on_line(h->in);
                if (h->conn < 0)
                    return true;
            }
            else if (h->in_len < sizeof(h->in) - 1)
            {
                h->in[h->in_len++] = buf[i];
            }
        }
    }
}

static int helper_pollfds(Helper *h, struct pollfd *fds, int max)
{
    int n = 0;
    if (n < max && h->listener >= 0 && h->conn < 0)
        fds[n++] = (struct pollfd){ h->listener, POLLIN, 0 };
    if (n < max && h->conn >= 0)
        fds[n++] = (struct pollfd){ h->conn, POLLIN, 0 };
    return n;
}

static void stream_end(const char *error)
{
    if (VC.st_state == SS_OFF)
        return;
    helper_stop(&VC.st);
    if (VC.st_key[0])
    {
        char json[256];
        snprintf(json, sizeof(json), "{\"op\":19,\"d\":{\"stream_key\":\"%s\"}}", VC.st_key);
        gw_send(json);
    }
    VC.st_state = SS_OFF;
    VC.st_key[0] = VC.st_server[0] = VC.st_token[0] = VC.st_endpoint[0] = '\0';
    VC.st_viewers = 0;
    snprintf(VC.st_error, sizeof(VC.st_error), "%s", error ? error : "");
    changed();
}

static void stop_helper(void)
{
    helper_stop(&VC.voice);
    VC.n_speaking = 0;
    VC.encrypted = false;
    stream_end(NULL);
}

static void fail(const char *msg)
{
    stop_helper();
    if (VC.state != VC_OFF)
        send_voice_state(VC.guild, 0);
    snprintf(VC.error, sizeof(VC.error), "%s", msg);
    VC.state = VC_FAILED;
    changed();
}

static void start_helper(void)
{
    char user[32], channel[32], server[32], mute[4], deaf[4];
    snprintf(user, sizeof(user), "%llu", (unsigned long long)S.me.id);
    snprintf(channel, sizeof(channel), "%llu", (unsigned long long)VC.channel);
    snprintf(server, sizeof(server), "%llu", (unsigned long long)(VC.guild ? VC.guild : VC.channel));
    snprintf(mute, sizeof(mute), "%d", VC.muted);
    snprintf(deaf, sizeof(deaf), "%d", VC.deafened);
    const char *argv[] = {
        HELPER, "--endpoint", VC.endpoint, "--server", server, "--session", VC.session, "--token", VC.token,
        "--user", user, "--channel", channel, "--ctl", "", "--mute", mute, "--deaf", deaf, NULL,
    };
    if (!helper_start(&VC.voice, argv))
    {
        fail("Could not start the voice helper.");
        return;
    }
    VC.state = VC_CONNECTING;
    changed();
}

static void maybe_start(void)
{
    if ((VC.state == VC_JOINING || VC.state == VC_CONNECTING) && VC.session[0] && VC.endpoint[0] && VC.token[0] &&
        VC.voice.pid == 0)
        start_helper();
}

void vc_join(Channel *ch)
{
    if (!ch || !S.me.id)
        return;
    if (ch->id == VC.channel && (VC.state == VC_JOINING || VC.state == VC_CONNECTING || VC.state == VC_CONNECTED))
        return;
    stop_helper();
    VC.guild = ch->guild_id;
    VC.channel = ch->id;
    VC.session[0] = VC.token[0] = VC.endpoint[0] = VC.error[0] = '\0';
    VC.st_error[0] = '\0';
    VC.state = VC_JOINING;
    VC.join_at = uptime_ms();
    send_voice_state(VC.guild, VC.channel);
    changed();
}

void vc_leave(void)
{
    if (VC.state == VC_OFF)
        return;
    stop_helper();
    if (VC.state != VC_FAILED)
        send_voice_state(VC.guild, 0);
    VC.state = VC_OFF;
    VC.channel = 0;
    VC.error[0] = '\0';
    changed();
}

void vc_reset(void)
{
    helper_stop(&VC.st);
    VC.st_state = SS_OFF;
    VC.st_key[0] = '\0';
    stop_helper();
    VC.state = VC_OFF;
    VC.channel = 0;
    changed();
}

void vc_set_mute(bool muted)
{
    VC.muted = muted;
    if (!muted && VC.deafened)
        VC.deafened = false;
    char line[16];
    snprintf(line, sizeof(line), "mute %d\n", VC.muted);
    helper_write(&VC.voice, line);
    snprintf(line, sizeof(line), "deaf %d\n", VC.deafened);
    helper_write(&VC.voice, line);
    if (VC.state == VC_CONNECTED || VC.state == VC_CONNECTING)
        send_voice_state(VC.guild, VC.channel);
    changed();
}

void vc_set_deaf(bool deafened)
{
    VC.deafened = deafened;
    char line[16];
    snprintf(line, sizeof(line), "deaf %d\n", VC.deafened);
    helper_write(&VC.voice, line);
    if (VC.state == VC_CONNECTED || VC.state == VC_CONNECTING)
        send_voice_state(VC.guild, VC.channel);
    changed();
}

void vc_on_state_update(Json *d)
{
    if (json_id(d, "user_id") != S.me.id || VC.state == VC_OFF || VC.state == VC_FAILED)
        return;
    Snowflake channel = json_id(d, "channel_id");
    if (!channel)
    {
        if (VC.state != VC_JOINING)
        {
            stop_helper();
            VC.state = VC_OFF;
            VC.channel = 0;
            changed();
        }
        return;
    }
    const char *session = json_str(d, "session_id");
    if (session)
        snprintf(VC.session, sizeof(VC.session), "%s", session);
    if (channel != VC.channel)
    {
        stream_end(NULL);
        VC.channel = channel;
        changed();
    }
    maybe_start();
}

void vc_on_server_update(Json *d)
{
    if (VC.state == VC_OFF || VC.state == VC_FAILED)
        return;
    Snowflake id = json_id(d, "guild_id");
    if (!id)
        id = json_id(d, "channel_id");
    if (id != (VC.guild ? VC.guild : VC.channel))
        return;
    const char *token = json_str(d, "token");
    const char *endpoint = json_str(d, "endpoint");
    if (!token || !endpoint)
    {
        stop_helper();
        VC.endpoint[0] = '\0';
        VC.state = VC_JOINING;
        VC.join_at = uptime_ms();
        changed();
        return;
    }
    snprintf(VC.token, sizeof(VC.token), "%s", token);
    snprintf(VC.endpoint, sizeof(VC.endpoint), "%s", endpoint);
    if (VC.voice.pid)
        helper_stop(&VC.voice);
    if (VC.state == VC_CONNECTED)
        VC.state = VC_CONNECTING;
    maybe_start();
}

static void stream_maybe_start(void)
{
    if (VC.st_state != SS_STARTING || VC.st.pid || !VC.st_server[0] || !VC.st_token[0] || !VC.st_endpoint[0] ||
        !VC.session[0])
        return;
    char user[32], channel[32];
    snprintf(user, sizeof(user), "%llu", (unsigned long long)S.me.id);
    snprintf(channel, sizeof(channel), "%llu", (unsigned long long)VC.channel);
    const char *argv[] = {
        HELPER, "--endpoint", VC.st_endpoint, "--server", VC.st_server, "--session", VC.session, "--token",
        VC.st_token, "--user", user, "--channel", channel, "--ctl", "", "--stream", "1", NULL,
    };
    if (!helper_start(&VC.st, argv))
        stream_end("Could not start the stream helper.");
}

void vc_stream_start(void)
{
    if (VC.state != VC_CONNECTED || VC.st_state != SS_OFF || !S.me.id)
        return;
    if (VC.guild)
        snprintf(VC.st_key, sizeof(VC.st_key), "guild:%llu:%llu:%llu", (unsigned long long)VC.guild,
                 (unsigned long long)VC.channel, (unsigned long long)S.me.id);
    else
        snprintf(VC.st_key, sizeof(VC.st_key), "call:%llu:%llu", (unsigned long long)VC.channel,
                 (unsigned long long)S.me.id);

    char json[320], g[32] = "null";
    if (VC.guild)
        snprintf(g, sizeof(g), "\"%llu\"", (unsigned long long)VC.guild);
    snprintf(json, sizeof(json),
             "{\"op\":18,\"d\":{\"type\":\"%s\",\"guild_id\":%s,\"channel_id\":\"%llu\",\"preferred_region\":null}}",
             VC.guild ? "guild" : "call", g, (unsigned long long)VC.channel);
    gw_send(json);
    snprintf(json, sizeof(json), "{\"op\":22,\"d\":{\"stream_key\":\"%s\",\"paused\":false}}", VC.st_key);
    gw_send(json);

    VC.st_state = SS_STARTING;
    VC.st_server[0] = VC.st_token[0] = VC.st_endpoint[0] = VC.st_error[0] = '\0';
    VC.st_viewers = 0;
    VC.st_at = uptime_ms();
    changed();
}

void vc_stream_stop(void)
{
    stream_end(NULL);
}

static int count_ids(Json *a)
{
    int n = 0;
    for (Json *v = a && a->type == JSON_ARRAY ? a->child : NULL; v; v = v->next)
        n++;
    return n;
}

static const char *stream_reason(const char *reason)
{
    if (!reason || strcmp(reason, "user_requested") == 0)
        return NULL;
    if (strcmp(reason, "stream_full") == 0)
        return "The stream is full.";
    if (strcmp(reason, "unauthorized") == 0)
        return "You can't stream in this channel.";
    return "The stream was ended by Discord.";
}

void vc_on_stream_event(const char *t, Json *d)
{
    const char *key = json_str(d, "stream_key");
    if (VC.st_state == SS_OFF || !key || strcmp(key, VC.st_key) != 0)
        return;
    if (strcmp(t, "STREAM_CREATE") == 0)
    {
        const char *server = json_str(d, "rtc_server_id");
        if (server)
            snprintf(VC.st_server, sizeof(VC.st_server), "%s", server);
        VC.st_viewers = count_ids(json_get(d, "viewer_ids"));
        stream_maybe_start();
        changed();
    }
    else if (strcmp(t, "STREAM_SERVER_UPDATE") == 0)
    {
        const char *token = json_str(d, "token"), *endpoint = json_str(d, "endpoint");
        if (!token || !endpoint)
            return;
        snprintf(VC.st_token, sizeof(VC.st_token), "%s", token);
        snprintf(VC.st_endpoint, sizeof(VC.st_endpoint), "%s", endpoint);
        if (VC.st.pid)
        {
            helper_stop(&VC.st);
            VC.st_state = SS_STARTING;
            VC.st_at = uptime_ms();
        }
        stream_maybe_start();
        changed();
    }
    else if (strcmp(t, "STREAM_UPDATE") == 0)
    {
        VC.st_viewers = count_ids(json_get(d, "viewer_ids"));
        changed();
    }
    else if (strcmp(t, "STREAM_DELETE") == 0)
    {
        VC.st_key[0] = '\0';
        stream_end(stream_reason(json_str(d, "reason")));
    }
}

static void set_speaking(Snowflake user, bool on)
{
    for (int i = 0; i < VC.n_speaking; i++)
        if (VC.speaking[i] == user)
        {
            if (!on)
            {
                VC.speaking[i] = VC.speaking[--VC.n_speaking];
                changed();
            }
            return;
        }
    if (on && VC.n_speaking < MAX_SPEAKING)
    {
        VC.speaking[VC.n_speaking++] = user;
        changed();
    }
}

static void voice_line(char *line)
{
    if (strcmp(line, "state connected") == 0)
    {
        VC.state = VC_CONNECTED;
        changed();
    }
    else if (strcmp(line, "state connecting") == 0)
    {
        VC.state = VC_CONNECTING;
        changed();
    }
    else if (strncmp(line, "error ", 6) == 0)
    {
        fail(line + 6);
    }
    else if (strncmp(line, "speaking ", 9) == 0)
    {
        char *end;
        Snowflake user = strtoull(line + 9, &end, 10);
        set_speaking(user, atoi(end) != 0);
    }
    else if (strncmp(line, "e2ee ", 5) == 0)
    {
        VC.encrypted = strtoull(line + 5, NULL, 10) != 0;
        changed();
    }
}

static void stream_line(char *line)
{
    if (strcmp(line, "state connected") == 0)
    {
        VC.st_state = SS_LIVE;
        changed();
    }
    else if (strcmp(line, "state connecting") == 0 && VC.st_state == SS_LIVE)
    {
        VC.st_state = SS_STARTING;
        VC.st_at = uptime_ms();
        changed();
    }
    else if (strncmp(line, "error ", 6) == 0)
    {
        stream_end(line + 6);
    }
}

void vc_step(void)
{
    if (VC.state == VC_JOINING && uptime_ms() - VC.join_at > JOIN_TIMEOUT)
    {
        fail("Discord did not answer the voice request.");
        return;
    }
    if (VC.st_state == SS_STARTING && uptime_ms() - VC.st_at > STREAM_TIMEOUT)
        stream_end("Discord did not start the stream.");

    if (!helper_step(&VC.voice, voice_line) && VC.state != VC_OFF && VC.state != VC_FAILED)
    {
        fail("The voice connection ended.");
        return;
    }
    if (!helper_step(&VC.st, stream_line) && VC.st_state != SS_OFF)
        stream_end("The stream connection ended.");
}

int vc_pollfds(struct pollfd *fds, int max)
{
    int n = helper_pollfds(&VC.voice, fds, max);
    return n + helper_pollfds(&VC.st, fds + n, max - n);
}

VcState vc_state(void) { return VC.state; }
Snowflake vc_channel(void) { return VC.channel; }
bool vc_muted(void) { return VC.muted; }
bool vc_deafened(void) { return VC.deafened; }
bool vc_encrypted(void) { return VC.encrypted; }
const char *vc_error(void) { return VC.error; }
StreamState vc_stream_state(void) { return VC.st_state; }
int vc_stream_viewers(void) { return VC.st_viewers; }
const char *vc_stream_error(void) { return VC.st_error; }

bool vc_speaking(Snowflake user)
{
    for (int i = 0; i < VC.n_speaking; i++)
        if (VC.speaking[i] == user)
            return true;
    return false;
}

bool vc_take_changed(void)
{
    bool c = VC.changed;
    VC.changed = false;
    return c;
}
