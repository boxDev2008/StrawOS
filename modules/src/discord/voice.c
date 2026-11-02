#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <socket.h>

#include "discord.h"

#define HELPER       "/bin/discord-voice.elf"
#define JOIN_TIMEOUT 15000
#define MAX_SPEAKING 64

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

    int       listener;
    int       conn;
    char      ctl_name[48];
    pid_t     pid;
    char      in[1024];
    size_t    in_len;

    Snowflake speaking[MAX_SPEAKING];
    int       n_speaking;
    bool      encrypted;
    char      error[160];
    bool      changed;
    uint64_t  join_at;
    int       spawns;
}
VC = { .listener = -1, .conn = -1 };

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

static void ctl_write(const char *line)
{
    if (VC.conn >= 0)
        write(VC.conn, line, strlen(line));
}

static void stop_helper(void)
{
    if (VC.conn >= 0)
    {
        ctl_write("quit\n");
        close(VC.conn);
    }
    if (VC.listener >= 0)
        close(VC.listener);
    if (VC.pid > 0)
        kill(VC.pid);
    VC.conn = VC.listener = -1;
    VC.pid = 0;
    VC.in_len = 0;
    VC.n_speaking = 0;
    VC.encrypted = false;
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
    stop_helper();
    snprintf(VC.ctl_name, sizeof(VC.ctl_name), "discord-voice-%d-%d", (int)getpid(), ++VC.spawns);
    VC.listener = socket(SOCK_NONBLOCK);
    if (VC.listener < 0 || bind(VC.listener, VC.ctl_name) != 0 || listen(VC.listener) != 0)
    {
        fail("Could not open the voice control socket.");
        return;
    }

    char user[32], channel[32], server[32], mute[4], deaf[4];
    snprintf(user, sizeof(user), "%llu", (unsigned long long)S.me.id);
    snprintf(channel, sizeof(channel), "%llu", (unsigned long long)VC.channel);
    snprintf(server, sizeof(server), "%llu", (unsigned long long)(VC.guild ? VC.guild : VC.channel));
    snprintf(mute, sizeof(mute), "%d", VC.muted);
    snprintf(deaf, sizeof(deaf), "%d", VC.deafened);
    const char *argv[] = {
        HELPER, "--endpoint", VC.endpoint, "--server", server, "--session", VC.session, "--token", VC.token,
        "--user", user, "--channel", channel, "--ctl", VC.ctl_name, "--mute", mute, "--deaf", deaf, NULL,
    };
    VC.pid = spawn(HELPER, argv);
    if (!VC.pid)
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
        VC.pid == 0)
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
    ctl_write(line);
    snprintf(line, sizeof(line), "deaf %d\n", VC.deafened);
    ctl_write(line);
    if (VC.state == VC_CONNECTED || VC.state == VC_CONNECTING)
        send_voice_state(VC.guild, VC.channel);
    changed();
}

void vc_set_deaf(bool deafened)
{
    VC.deafened = deafened;
    char line[16];
    snprintf(line, sizeof(line), "deaf %d\n", VC.deafened);
    ctl_write(line);
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
    if (VC.pid)
        stop_helper();
    if (VC.state == VC_CONNECTED)
        VC.state = VC_CONNECTING;
    maybe_start();
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

static void ctl_line(char *line)
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

void vc_step(void)
{
    if (VC.state == VC_JOINING && uptime_ms() - VC.join_at > JOIN_TIMEOUT)
    {
        fail("Discord did not answer the voice request.");
        return;
    }
    if (VC.listener >= 0 && VC.conn < 0)
    {
        int c = accept(VC.listener);
        if (c >= 0)
            VC.conn = c;
    }
    if (VC.conn < 0)
        return;
    for (;;)
    {
        char buf[256];
        ssize_t n = read(VC.conn, buf, sizeof(buf));
        if (n == 0)
        {
            if (VC.state != VC_OFF && VC.state != VC_FAILED)
                fail("The voice connection ended.");
            return;
        }
        if (n < 0)
            return;
        for (ssize_t i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                VC.in[VC.in_len] = '\0';
                VC.in_len = 0;
                ctl_line(VC.in);
                if (VC.conn < 0)
                    return;
            }
            else if (VC.in_len < sizeof(VC.in) - 1)
            {
                VC.in[VC.in_len++] = buf[i];
            }
        }
    }
}

int vc_pollfds(struct pollfd *fds, int max)
{
    int n = 0;
    if (n < max && VC.listener >= 0 && VC.conn < 0)
        fds[n++] = (struct pollfd){ VC.listener, POLLIN, 0 };
    if (n < max && VC.conn >= 0)
        fds[n++] = (struct pollfd){ VC.conn, POLLIN, 0 };
    return n;
}

VcState vc_state(void) { return VC.state; }
Snowflake vc_channel(void) { return VC.channel; }
bool vc_muted(void) { return VC.muted; }
bool vc_deafened(void) { return VC.deafened; }
bool vc_encrypted(void) { return VC.encrypted; }
const char *vc_error(void) { return VC.error; }

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
