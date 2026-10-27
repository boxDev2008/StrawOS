#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "discord.h"

#define TOKEN_DIR   "/config/discord"
#define TOKEN_PATH  "/config/discord/token"

#define GUILD_W     72
#define SIDEBAR_W   240
#define HEADER_H    48
#define USER_H      52
#define INPUT_H     44
#define INPUT_GAP   24
#define AVATAR      40
#define GUTTER      72
#define MAX_HITS    1024

typedef enum
{
    SCREEN_LOGIN,
    SCREEN_CONNECTING,
    SCREEN_MAIN,
}
Screen;

typedef enum
{
    HIT_HOME,
    HIT_GUILD,
    HIT_CHANNEL,
    HIT_TOKEN,
    HIT_LOGIN,
    HIT_QR_RETRY,
    HIT_LOGOUT,
    HIT_INPUT,
    HIT_RETRY_LOAD,
    HIT_GUILD_AREA,
    HIT_SIDEBAR_AREA,
    HIT_MESSAGES_AREA,
}
HitKind;

typedef struct
{
    HitKind   kind;
    SxRect    r;
    Snowflake id;
}
Hit;

typedef struct
{
    char text[2000];
    int  len;
    int  cursor;
    int  scroll;
}
Field;

typedef struct
{
    Snowflake guild;
    Snowflake channel;
}
LastChannel;

static struct
{
    SxWindow   *win;
    int         w, h;
    Screen      screen;
    bool        dirty;
    bool        focused;

    Snowflake   guild;
    Snowflake   channel;
    LastChannel last[64];

    int         guild_scroll;
    int         side_scroll;
    int         msg_scroll;
    int         msg_total;
    Snowflake   msg_newest;
    Snowflake   msg_channel;

    Field       token;
    Field       input;
    bool        token_focus;

    Hit         hits[MAX_HITS];
    int         hit_count;
    int         hover_x, hover_y;

    QrCode      qr;
    char        qr_for[160];
    bool        ra_start_pending;

    char        login_error[192];
    char        status[192];
    uint64_t    status_at;

    bool        caret_on;
    uint64_t    caret_at;
    int         seen_version;
}
U;

static void set_status(const char *msg)
{
    snprintf(U.status, sizeof(U.status), "%s", msg);
    U.status_at = uptime_ms();
    U.dirty = true;
}

static void add_hit(HitKind kind, SxRect r, Snowflake id)
{
    if (U.hit_count < MAX_HITS)
        U.hits[U.hit_count++] = (Hit){ kind, r, id };
}

static Hit *hit_at(int x, int y)
{
    for (int i = U.hit_count - 1; i >= 0; i--)
        if (sx_rect_contains(U.hits[i].r, x, y))
            return &U.hits[i];
    return NULL;
}

static bool hovering(SxRect r)
{
    return sx_rect_contains(r, U.hover_x, U.hover_y);
}

static void field_insert(Field *f, const char *s, int n)
{
    if (f->len + n >= (int)sizeof(f->text))
        return;
    memmove(f->text + f->cursor + n, f->text + f->cursor, (size_t)(f->len - f->cursor + 1));
    memcpy(f->text + f->cursor, s, (size_t)n);
    f->len += n;
    f->cursor += n;
}

static int utf8_prev(const char *s, int i)
{
    if (i <= 0)
        return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
        i--;
    return i;
}

static int utf8_next(const char *s, int len, int i)
{
    if (i >= len)
        return len;
    i++;
    while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80)
        i++;
    return i;
}

static bool field_key(Field *f, const SxEvent *ev)
{
    switch (ev->key)
    {
        case SX_KEY_ENTER:
            return true;
        case SX_KEY_BACKSPACE:
            if (f->cursor > 0)
            {
                int p = (ev->mods & SX_MOD_CTRL) ? 0 : utf8_prev(f->text, f->cursor);
                memmove(f->text + p, f->text + f->cursor, (size_t)(f->len - f->cursor + 1));
                f->len -= f->cursor - p;
                f->cursor = p;
            }
            return false;
        case SX_KEY_DELETE:
            if (f->cursor < f->len)
            {
                int n = utf8_next(f->text, f->len, f->cursor);
                memmove(f->text + f->cursor, f->text + n, (size_t)(f->len - n + 1));
                f->len -= n - f->cursor;
            }
            return false;
        case SX_KEY_LEFT:
            f->cursor = utf8_prev(f->text, f->cursor);
            return false;
        case SX_KEY_RIGHT:
            f->cursor = utf8_next(f->text, f->len, f->cursor);
            return false;
        case SX_KEY_HOME:
            f->cursor = 0;
            return false;
        case SX_KEY_END:
            f->cursor = f->len;
            return false;
        default:
            break;
    }
    if ((ev->mods & SX_MOD_CTRL) && (ev->ch == 'u' || ev->ch == 'U' || ev->ch == 21))
    {
        f->len = f->cursor = 0;
        f->text[0] = '\0';
        return false;
    }
    if (ev->ch >= 32 && ev->ch < 127 && !(ev->mods & (SX_MOD_CTRL | SX_MOD_ALT)))
    {
        char c = (char)ev->ch;
        field_insert(f, &c, 1);
    }
    return false;
}

static void field_clear(Field *f)
{
    f->len = f->cursor = f->scroll = 0;
    f->text[0] = '\0';
}

static void draw_field(SxCanvas *c, Field *f, SxRect r, bool focused, bool masked, const char *placeholder, int px)
{
    char shown[sizeof(f->text) * 3];
    int cursor_bytes = f->cursor;
    if (masked)
    {
        int n = 0;
        for (int i = 0; i < f->len && n + 3 < (int)sizeof(shown); i++)
        {
            if (i == f->cursor)
                cursor_bytes = n;
            memcpy(shown + n, "\xE2\x80\xA2", 3);
            n += 3;
        }
        if (f->cursor >= f->len)
            cursor_bytes = n;
        shown[n] = '\0';
    }
    else
    {
        memcpy(shown, f->text, (size_t)f->len + 1);
    }
    int len = (int)strlen(shown);
    int inner = r.w - 24;
    int cx = font_width(FACE_REGULAR, px, shown, cursor_bytes);
    if (cx - f->scroll > inner)
        f->scroll = cx - inner;
    if (cx < f->scroll)
        f->scroll = cx;
    int full = font_width(FACE_REGULAR, px, shown, len);
    if (full - f->scroll < inner && f->scroll > 0)
        f->scroll = full - inner > 0 ? full - inner : 0;

    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, sx_rect(r.x + 10, r.y, r.w - 20, r.h)));
    int base = r.y + (r.h - font_line_height(FACE_REGULAR, px)) / 2 + font_ascent(FACE_REGULAR, px);
    if (f->len == 0 && placeholder)
        font_draw(c, FACE_REGULAR, px, r.x + 12, base, placeholder, (int)strlen(placeholder), COL_FAINT);
    else
        font_draw(c, FACE_REGULAR, px, r.x + 12 - f->scroll, base, shown, len, COL_TEXT);
    if (focused && U.caret_on)
    {
        int lh = font_line_height(FACE_REGULAR, px);
        sx_fill_rect(c, r.x + 12 + cx - f->scroll, r.y + (r.h - lh) / 2 + 1, 1, lh - 2, COL_HEADER);
    }
    sx_canvas_set_clip(c, old);
}

static void text_center(SxCanvas *c, int face, int px, SxRect r, const char *s, uint32_t color)
{
    int w = font_width(face, px, s, (int)strlen(s));
    int y = r.y + (r.h - font_line_height(face, px)) / 2 + font_ascent(face, px);
    font_draw(c, face, px, r.x + (r.w - w) / 2, y, s, (int)strlen(s), color);
}

static int text_wrap(SxCanvas *c, int face, int px, int x, int y, int w, const char *s, uint32_t color, bool center)
{
    int lh = font_line_height(face, px);
    int len = (int)strlen(s);
    int i = 0;
    int lines = 0;
    while (i < len)
    {
        int fit = font_fit(face, px, s + i, len - i, w);
        if (fit < len - i)
        {
            int k = fit;
            while (k > 0 && s[i + k] != ' ')
                k--;
            if (k > 0)
                fit = k;
        }
        if (fit <= 0)
            fit = 1;
        if (c)
        {
            int lw = font_width(face, px, s + i, fit);
            int lx = center ? x + (w - lw) / 2 : x;
            font_draw(c, face, px, lx, y + lines * lh + font_ascent(face, px), s + i, fit, color);
        }
        i += fit;
        while (i < len && s[i] == ' ')
            i++;
        lines++;
    }
    return lines * lh;
}

static void save_token(const char *token)
{
    mkdir("/config", 0755);
    mkdir(TOKEN_DIR, 0755);
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f)
        return;
    fputs(token, f);
    fclose(f);
}

static bool load_token(char *out, size_t size)
{
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f)
        return false;
    size_t n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = '\0';
    return n > 0;
}

static void begin_login(const char *token)
{
    char t[256];
    snprintf(t, sizeof(t), "%s", token);
    char *s = t;
    while (*s == ' ' || *s == '"')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '"'))
        s[--n] = '\0';
    if (!*s)
    {
        snprintf(U.login_error, sizeof(U.login_error), "Enter a token first.");
        U.dirty = true;
        return;
    }
    ra_stop();
    U.login_error[0] = '\0';
    store_reset();
    gw_start(s);
    U.screen = SCREEN_CONNECTING;
    U.dirty = true;
}

static void show_login(const char *error)
{
    gw_stop();
    store_reset();
    U.screen = SCREEN_LOGIN;
    U.guild = U.channel = 0;
    memset(U.last, 0, sizeof(U.last));
    snprintf(U.login_error, sizeof(U.login_error), "%s", error ? error : "");
    U.ra_start_pending = true;
    U.token_focus = true;
    U.dirty = true;
}

static void logout(void)
{
    unlink(TOKEN_PATH);
    field_clear(&U.token);
    show_login(NULL);
}

static void remember_channel(Snowflake guild, Snowflake channel)
{
    LastChannel *slot = &U.last[0];
    for (int i = 0; i < 64; i++)
    {
        if (U.last[i].guild == guild)
        {
            U.last[i].channel = channel;
            return;
        }
        if (!U.last[i].guild)
        {
            slot = &U.last[i];
            break;
        }
    }
    slot->guild = guild;
    slot->channel = channel;
}

static Snowflake remembered_channel(Snowflake guild)
{
    for (int i = 0; i < 64; i++)
        if (U.last[i].guild == guild)
            return U.last[i].channel;
    return 0;
}

typedef struct
{
    Channel *ch;
    bool     category;
}
SideItem;

static int side_items(Guild *g, SideItem *out, int max)
{
    int n = 0;
    for (int i = 0; i < g->channel_count && n < max; i++)
    {
        Channel *ch = g->channels[i];
        if (ch->parent_id == 0 && ch->type != CH_CATEGORY && store_channel_visible(g, ch))
            out[n++] = (SideItem){ ch, false };
    }
    for (int i = 0; i < g->channel_count && n < max; i++)
    {
        Channel *cat = g->channels[i];
        if (cat->type != CH_CATEGORY)
            continue;
        int header = n;
        out[n++] = (SideItem){ cat, true };
        for (int k = 0; k < g->channel_count && n < max; k++)
        {
            Channel *ch = g->channels[k];
            if (ch->parent_id == cat->id && ch->type != CH_CATEGORY && store_channel_visible(g, ch))
                out[n++] = (SideItem){ ch, false };
        }
        if (n == header + 1)
            n = header;
    }
    return n;
}

typedef struct
{
    Snowflake channel;
    bool      older;
}
LoadCtx;

static void on_messages(int status, const uint8_t *body, size_t len, void *user)
{
    LoadCtx *ctx = user;
    Channel *ch = store_channel(ctx->channel);
    if (ch)
    {
        ch->loading = false;
        if (status == 200)
        {
            JsonDoc *doc = json_parse((const char *)body, len);
            store_load_messages(ch, json_root(doc), ctx->older);
            json_free(doc);
            ch->load_failed = false;
        }
        else if (!ctx->older)
        {
            ch->load_failed = true;
            if (status == 403)
                snprintf(ch->error, sizeof(ch->error), "You don't have permission to read this channel.");
            else if (status == 0)
                snprintf(ch->error, sizeof(ch->error), "Couldn't reach Discord.");
            else
                snprintf(ch->error, sizeof(ch->error), "Couldn't load messages (HTTP %d).", status);
        }
        else
        {
            ch->has_more = false;
        }
        S.version++;
    }
    free(ctx);
}

static void load_messages(Channel *ch, bool older)
{
    if (ch->loading || (older && (!ch->has_more || ch->message_count == 0)))
        return;
    LoadCtx *ctx = malloc(sizeof(LoadCtx));
    if (!ctx)
        return;
    ctx->channel = ch->id;
    ctx->older = older;
    char path[160];
    if (older)
    {
        Snowflake oldest = 0;
        for (int i = 0; i < ch->message_count; i++)
            if (!ch->messages[i]->pending)
            {
                oldest = ch->messages[i]->id;
                break;
            }
        snprintf(path, sizeof(path), "/channels/%llu/messages?limit=50&before=%llu",
                 (unsigned long long)ch->id, (unsigned long long)oldest);
    }
    else
    {
        snprintf(path, sizeof(path), "/channels/%llu/messages?limit=50", (unsigned long long)ch->id);
    }
    ch->loading = true;
    ch->load_failed = false;
    if (!api_request("GET", path, NULL, on_messages, ctx))
    {
        ch->loading = false;
        free(ctx);
    }
}

static void select_channel(Channel *ch)
{
    if (!ch)
        return;
    U.channel = ch->id;
    U.msg_scroll = 0;
    U.msg_channel = 0;
    remember_channel(U.guild, ch->id);
    if (!ch->loaded && !ch->loading)
        load_messages(ch, false);
    char title[96];
    char name[80];
    store_channel_title(ch, name, sizeof(name));
    snprintf(title, sizeof(title), "%s%s - Discord", ch->guild_id ? "#" : "@", name);
    sx_window_set_title(U.win, title);
    U.dirty = true;
}

static void select_guild(Snowflake id)
{
    U.guild = id;
    U.side_scroll = 0;
    Snowflake want = remembered_channel(id);
    Channel *pick = want ? store_channel(want) : NULL;
    if (!pick && id)
    {
        Guild *g = store_guild(id);
        SideItem items[512];
        int n = g ? side_items(g, items, 512) : 0;
        for (int i = 0; i < n && !pick; i++)
            if (!items[i].category && store_channel_is_text(items[i].ch))
                pick = items[i].ch;
    }
    if (!pick && !id && S.dm_count)
        pick = S.dms[0];
    if (pick)
        select_channel(pick);
    else
    {
        U.channel = 0;
        sx_window_set_title(U.win, "Discord");
    }
    U.dirty = true;
}

typedef struct
{
    Snowflake channel;
    char      nonce[32];
}
SendCtx;

static void on_sent(int status, const uint8_t *body, size_t len, void *user)
{
    (void)body;
    (void)len;
    SendCtx *ctx = user;
    if (status < 200 || status >= 300)
    {
        Channel *ch = store_channel(ctx->channel);
        if (ch)
            store_mark_failed(ch, ctx->nonce);
        if (status == 403)
            set_status("You don't have permission to send messages here.");
        else
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "Message failed to send (HTTP %d).", status);
            set_status(msg);
        }
    }
    free(ctx);
}

static void send_message(void)
{
    Channel *ch = store_channel(U.channel);
    if (!ch || U.input.len == 0)
        return;
    bool blank = true;
    for (int i = 0; i < U.input.len; i++)
        if (U.input.text[i] != ' ')
            blank = false;
    if (blank)
        return;
    SendCtx *ctx = malloc(sizeof(SendCtx));
    if (!ctx)
        return;
    ctx->channel = ch->id;
    uint64_t nonce = ((uint64_t)time(NULL) * 1000 - 1420070400000ULL) << 22;
    uint32_t r;
    random_bytes(&r, sizeof(r));
    nonce |= r & 0x3FFFFF;
    snprintf(ctx->nonce, sizeof(ctx->nonce), "%llu", (unsigned long long)nonce);

    Buf b = { 0 };
    buf_puts(&b, "{\"content\":");
    buf_json_string(&b, U.input.text);
    buf_printf(&b, ",\"nonce\":\"%s\",\"tts\":false,\"flags\":0}", ctx->nonce);
    char path[96];
    snprintf(path, sizeof(path), "/channels/%llu/messages", (unsigned long long)ch->id);
    if (api_request("POST", path, b.data, on_sent, ctx))
    {
        if (ch->loaded)
            store_add_pending(ch, U.input.text, ctx->nonce);
        field_clear(&U.input);
        U.msg_scroll = 0;
    }
    else
    {
        free(ctx);
    }
    buf_free(&b);
}

static void draw_initials(SxCanvas *c, const char *name, SxRect r, uint32_t color)
{
    char init[16];
    int n = 0;
    bool word = true;
    for (const char *p = name; *p && n < 3; p++)
    {
        if (*p == ' ')
        {
            word = true;
            continue;
        }
        if (word && ((unsigned char)*p < 0x80))
            init[n++] = *p;
        word = false;
    }
    init[n] = '\0';
    text_center(c, FACE_BOLD, n > 2 ? 13 : 16, r, init, color);
}

static void draw_avatar(SxCanvas *c, Snowflake uid, const char *avatar, const char *disc, int x, int y, int d)
{
    char url[300];
    user_avatar_url(uid, avatar, disc, url, sizeof(url));
    SxImage *img;
    if (img_request(url, d, d, &img) == IMG_READY && img)
        img_draw_circle(c, img, x, y, d);
    else
        sx_fill_circle(c, x + d / 2.0f, y + d / 2.0f, d / 2.0f, COL_SELECTED);
}

static void draw_guild_bar(SxCanvas *c)
{
    sx_fill_rect(c, 0, 0, GUILD_W, U.h, COL_GUILDBAR);
    SxRect area = sx_rect(0, 0, GUILD_W, U.h);
    add_hit(HIT_GUILD_AREA, area, 0);
    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, area));

    int y = 12 - U.guild_scroll;
    SxRect home = sx_rect(12, y, 48, 48);
    bool sel = U.guild == 0;
    bool hov = hovering(home);
    uint32_t bg = sel || hov ? COL_ACCENT : COL_MAIN;
    sx_fill_rounded_rect(c, home.x, home.y, 48, 48, sel || hov ? 16 : 24, bg);
    sx_fill_rounded_rect(c, home.x + 13, home.y + 15, 22, 15, 5, sel || hov ? COL_HEADER : COL_TEXT);
    sx_fill_rect(c, home.x + 17, home.y + 28, 5, 5, sel || hov ? COL_HEADER : COL_TEXT);
    if (sel)
        sx_fill_rounded_rect(c, -4, home.y + 4, 8, 40, 4, COL_HEADER);
    add_hit(HIT_HOME, home, 0);
    y += 56;
    sx_fill_rect(c, 24, y, 24, 2, COL_SELECTED);
    y += 10;

    for (int i = 0; i < S.guild_count; i++)
    {
        Guild *g = S.guilds[i];
        SxRect r = sx_rect(12, y, 48, 48);
        sel = U.guild == g->id;
        hov = hovering(r);
        bool drawn = false;
        if (g->icon)
        {
            char url[300];
            snprintf(url, sizeof(url), "%s/icons/%llu/%s.png?size=96", cdn_base, (unsigned long long)g->id, g->icon);
            SxImage *img;
            if (r.y + 48 >= 0 && r.y <= U.h && img_request(url, 48, 48, &img) == IMG_READY && img)
            {
                img_draw(c, img, r.x, r.y, sel || hov ? 16 : 24);
                drawn = true;
            }
        }
        if (!drawn)
        {
            sx_fill_rounded_rect(c, r.x, r.y, 48, 48, sel || hov ? 16 : 24, sel || hov ? COL_ACCENT : COL_MAIN);
            draw_initials(c, g->name, r, COL_HEADER);
        }
        if (sel)
            sx_fill_rounded_rect(c, -4, r.y + 4, 8, 40, 4, COL_HEADER);
        else if (hov)
            sx_fill_rounded_rect(c, -4, r.y + 14, 8, 20, 4, COL_HEADER);
        add_hit(HIT_GUILD, r, g->id);
        y += 56;
    }
    int content = y + U.guild_scroll + 12;
    if (U.guild_scroll > content - U.h)
        U.guild_scroll = content - U.h > 0 ? content - U.h : 0;
    sx_canvas_set_clip(c, old);
}

static void draw_hash(SxCanvas *c, int x, int y, uint32_t color)
{
    sx_draw_line(c, x + 7, y + 3, x + 5, y + 17, 1.8f, color);
    sx_draw_line(c, x + 13, y + 3, x + 11, y + 17, 1.8f, color);
    sx_draw_line(c, x + 3, y + 7.5f, x + 17, y + 7.5f, 1.8f, color);
    sx_draw_line(c, x + 2, y + 12.5f, x + 16, y + 12.5f, 1.8f, color);
}

static void draw_speaker(SxCanvas *c, int x, int y, uint32_t color)
{
    sx_fill_rect(c, x + 3, y + 7, 4, 6, color);
    for (int i = 0; i < 6; i++)
        sx_fill_rect(c, x + 7 + i, y + 7 - i, 1, 6 + i * 2, color);
    sx_draw_circle(c, x + 13, y + 10, 4, 1.4f, color);
}

static void draw_sidebar(SxCanvas *c)
{
    int x0 = GUILD_W;
    sx_fill_rect(c, x0, 0, SIDEBAR_W, U.h, COL_SIDEBAR);
    Guild *g = U.guild ? store_guild(U.guild) : NULL;

    SxRect head = sx_rect(x0, 0, SIDEBAR_W, HEADER_H);
    const char *title = g ? g->name : "Direct Messages";
    font_draw_ellipsis(c, FACE_BOLD, 15, x0 + 16, 30, title, SIDEBAR_W - 32, COL_HEADER);
    sx_fill_rect(c, x0, HEADER_H - 1, SIDEBAR_W, 1, COL_DIVIDER);
    (void)head;

    SxRect list = sx_rect(x0, HEADER_H, SIDEBAR_W, U.h - HEADER_H - USER_H);
    add_hit(HIT_SIDEBAR_AREA, list, 0);
    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, list));
    int y = HEADER_H + 8 - U.side_scroll;

    if (g)
    {
        SideItem items[1024];
        int n = side_items(g, items, 1024);
        for (int i = 0; i < n; i++)
        {
            Channel *ch = items[i].ch;
            if (items[i].category)
            {
                y += 12;
                char upper[100];
                int k = 0;
                for (const char *p = ch->name ? ch->name : ""; *p && k < 98; p++)
                    upper[k++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
                upper[k] = '\0';
                font_draw_ellipsis(c, FACE_BOLD, 12, x0 + 16, y + 14, upper, SIDEBAR_W - 32, COL_MUTED);
                y += 22;
                continue;
            }
            SxRect r = sx_rect(x0 + 8, y, SIDEBAR_W - 16, 32);
            bool text = store_channel_is_text(ch);
            bool sel = ch->id == U.channel;
            bool hov = text && hovering(r);
            if (sel)
                sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 4, COL_SELECTED);
            else if (hov)
                sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 4, COL_HOVER);
            uint32_t col = sel ? COL_HEADER : hov ? COL_TEXT : COL_MUTED;
            if (ch->type == CH_VOICE || ch->type == CH_STAGE)
                draw_speaker(c, r.x + 8, r.y + 6, COL_FAINT);
            else
                draw_hash(c, r.x + 8, r.y + 6, sel ? COL_TEXT : COL_FAINT);
            font_draw_ellipsis(c, sel ? FACE_BOLD : FACE_REGULAR, 15, r.x + 34, r.y + 21, ch->name ? ch->name : "",
                               r.w - 42, text ? col : COL_FAINT);
            if (text)
                add_hit(HIT_CHANNEL, r, ch->id);
            y += 34;
        }
    }
    else
    {
        for (int i = 0; i < S.dm_count; i++)
        {
            Channel *ch = S.dms[i];
            SxRect r = sx_rect(x0 + 8, y, SIDEBAR_W - 16, 42);
            bool sel = ch->id == U.channel;
            bool hov = hovering(r);
            if (r.y + r.h >= HEADER_H && r.y <= U.h)
            {
                if (sel)
                    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 4, COL_SELECTED);
                else if (hov)
                    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 4, COL_HOVER);
                if (ch->type == CH_DM && ch->recipient_count > 0)
                {
                    User *u = store_user(ch->recipients[0]);
                    draw_avatar(c, ch->recipients[0], u ? u->avatar : NULL, u ? u->discriminator : NULL, r.x + 8, r.y + 5, 32);
                }
                else
                {
                    sx_fill_circle(c, r.x + 24, r.y + 21, 16, COL_ACCENT);
                    draw_initials(c, "G", sx_rect(r.x + 8, r.y + 5, 32, 32), COL_HEADER);
                }
                char name[128];
                store_channel_title(ch, name, sizeof(name));
                font_draw_ellipsis(c, sel ? FACE_BOLD : FACE_REGULAR, 15, r.x + 50, r.y + 26, name, r.w - 58,
                                   sel ? COL_HEADER : hov ? COL_TEXT : COL_MUTED);
            }
            add_hit(HIT_CHANNEL, r, ch->id);
            y += 44;
        }
        if (S.dm_count == 0)
            text_wrap(c, FACE_REGULAR, 13, x0 + 16, y + 8, SIDEBAR_W - 32, "No direct messages yet.", COL_MUTED, false);
    }
    int content = y + U.side_scroll + 8 - HEADER_H;
    int view = list.h;
    if (U.side_scroll > content - view)
        U.side_scroll = content - view > 0 ? content - view : 0;
    sx_canvas_set_clip(c, old);

    int uy = U.h - USER_H;
    sx_fill_rect(c, x0, uy, SIDEBAR_W, USER_H, COL_PANEL);
    draw_avatar(c, S.me.id, S.me.avatar, S.me.discriminator, x0 + 10, uy + 10, 32);
    const char *me = S.me.global_name && *S.me.global_name ? S.me.global_name : S.me.username ? S.me.username : "";
    font_draw_ellipsis(c, FACE_BOLD, 14, x0 + 50, uy + 22, me, SIDEBAR_W - 130, COL_HEADER);
    font_draw_ellipsis(c, FACE_REGULAR, 12, x0 + 50, uy + 39, S.me.username ? S.me.username : "", SIDEBAR_W - 130, COL_MUTED);
    SxRect out = sx_rect(x0 + SIDEBAR_W - 74, uy + 12, 64, 28);
    sx_fill_rounded_rect(c, out.x, out.y, out.w, out.h, 4, hovering(out) ? COL_SELECTED : COL_HOVER);
    text_center(c, FACE_REGULAR, 12, out, "Log out", COL_TEXT);
    add_hit(HIT_LOGOUT, out, 0);
}

static bool is_image(const Attachment *a)
{
    if (a->content_type && strncmp(a->content_type, "image/", 6) == 0)
        return a->width > 0 && a->height > 0;
    const char *dot = a->filename ? strrchr(a->filename, '.') : NULL;
    return dot && a->width > 0 && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
                                   !strcasecmp(dot, ".gif") || !strcasecmp(dot, ".webp"));
}

static void fit_box(int w, int h, int max_w, int max_h, int *ow, int *oh)
{
    if (w <= 0 || h <= 0)
    {
        *ow = max_w < 300 ? max_w : 300;
        *oh = 200;
        return;
    }
    float s = 1.0f;
    if (w > max_w)
        s = (float)max_w / (float)w;
    if (h * s > max_h)
        s = (float)max_h / (float)h;
    *ow = (int)(w * s);
    *oh = (int)(h * s);
    if (*ow < 1)
        *ow = 1;
    if (*oh < 1)
        *oh = 1;
}

static void media_url(const char *proxy, int w, int h, const char *ctype, char *out, size_t size)
{
    bool webp = (ctype && strstr(ctype, "webp")) || (proxy && strstr(proxy, ".webp"));
    snprintf(out, size, "%s%cwidth=%d&height=%d%s", proxy, strchr(proxy, '?') ? '&' : '?', w, h, webp ? "&format=png" : "");
}

static bool is_system(const Message *m)
{
    return !(m->type == 0 || m->type == 19 || m->type == 20 || m->type == 23);
}

static const char *system_text(const Message *m)
{
    switch (m->type)
    {
        case 1:  return "added someone to the group.";
        case 2:  return "left the group.";
        case 3:  return "started a call.";
        case 4:  return "changed the channel name.";
        case 5:  return "changed the channel icon.";
        case 6:  return "pinned a message to this channel.";
        case 7:  return "joined the server.";
        case 8:  return "just boosted the server!";
        case 18: return "started a thread.";
        default: return "did something here.";
    }
}

static int embed_height(Message *m, int i, int width)
{
    Embed *e = &m->embeds[i];
    int inner = width - 32;
    if (inner > 420)
        inner = 420;
    int h = 16;
    if (e->author)
        h += 20;
    if (e->title)
        h += 22;
    if (e->description && i < 4)
    {
        if (!m->embed_layouts[i])
            m->embed_layouts[i] = rich_layout(e->description, inner, 0, COL_TEXT);
        h += rich_height(m->embed_layouts[i]) + 2;
    }
    if (e->image)
    {
        int iw, ih;
        fit_box(e->image_w, e->image_h, inner, 300, &iw, &ih);
        h += ih + 8;
    }
    return h;
}

static int message_height(Message *m, int content_w, Snowflake guild_id, bool grouped)
{
    if (is_system(m))
        return grouped ? 26 : 30;
    if (!m->layout || m->layout_width != content_w)
    {
        rich_free(m->layout);
        uint32_t color = m->failed ? COL_RED : m->pending ? COL_MUTED : COL_TEXT;
        m->layout = rich_layout(m->content, content_w, guild_id, color);
        m->layout_width = content_w;
        for (int i = 0; i < 4; i++)
        {
            rich_free(m->embed_layouts[i]);
            m->embed_layouts[i] = NULL;
        }
    }
    int h = grouped ? 2 : 16;
    if (m->is_reply)
        h += 20;
    if (!grouped)
        h += 22;
    h += rich_height(m->layout);
    for (int i = 0; i < m->attachment_count; i++)
    {
        Attachment *a = &m->attachments[i];
        if (is_image(a))
        {
            int w, ih;
            fit_box(a->width, a->height, content_w < 400 ? content_w : 400, 300, &w, &ih);
            h += ih + 6;
        }
        else
        {
            h += 58;
        }
    }
    for (int i = 0; i < m->embed_count; i++)
        h += embed_height(m, i, content_w) + 6;
    return h + 2;
}

static void format_time(time_t t, char *out, size_t size, bool full)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    time_t now = time(NULL);
    struct tm today;
    gmtime_r(&now, &today);
    if (!full)
        strftime(out, size, "%H:%M", &tm);
    else if (tm.tm_year == today.tm_year && tm.tm_yday == today.tm_yday)
        strftime(out, size, "Today at %H:%M", &tm);
    else if (tm.tm_year == today.tm_year && tm.tm_yday == today.tm_yday - 1)
        strftime(out, size, "Yesterday at %H:%M", &tm);
    else
        strftime(out, size, "%d/%m/%Y %H:%M", &tm);
}

static void draw_embed(SxCanvas *c, Message *m, int i, int x, int y, int width)
{
    Embed *e = &m->embeds[i];
    int h = embed_height(m, i, width);
    int inner = width - 32;
    if (inner > 420)
        inner = 420;
    int box_w = inner + 32;
    uint32_t bar = e->color ? (0xFF000000u | e->color) : COL_GUILDBAR;
    sx_fill_rounded_rect(c, x, y, box_w, h, 4, COL_SIDEBAR);
    sx_fill_rounded_rect(c, x, y, 4, h, 2, bar);
    int cy = y + 8;
    if (e->author)
    {
        font_draw_ellipsis(c, FACE_BOLD, 13, x + 16, cy + 14, e->author, inner, COL_HEADER);
        cy += 20;
    }
    if (e->title)
    {
        font_draw_ellipsis(c, FACE_BOLD, 15, x + 16, cy + 16, e->title, inner, e->url ? COL_LINK : COL_HEADER);
        cy += 22;
    }
    if (e->description && i < 4 && m->embed_layouts[i])
    {
        rich_draw(c, m->embed_layouts[i], x + 16, cy);
        cy += rich_height(m->embed_layouts[i]) + 2;
    }
    if (e->image)
    {
        int iw, ih;
        fit_box(e->image_w, e->image_h, inner, 300, &iw, &ih);
        char url[1200];
        media_url(e->image, iw, ih, NULL, url, sizeof(url));
        SxImage *img;
        if (cy + ih >= c->clip.y && cy <= c->clip.y + c->clip.h && img_request(url, iw, ih, &img) == IMG_READY && img)
            img_draw(c, img, x + 16, cy + 4, 4);
        else
            sx_fill_rounded_rect(c, x + 16, cy + 4, iw, ih, 4, COL_PANEL);
    }
}

static void draw_message(SxCanvas *c, Message *m, int x, int y, int content_w, bool grouped)
{
    int cx = x + GUTTER;
    if (is_system(m))
    {
        int top = y + (grouped ? 4 : 8);
        sx_draw_line(c, x + 32, top + 10, x + 46, top + 10, 2, COL_GREEN);
        sx_draw_line(c, x + 41, top + 5, x + 46, top + 10, 2, COL_GREEN);
        sx_draw_line(c, x + 41, top + 15, x + 46, top + 10, 2, COL_GREEN);
        int w = font_draw(c, FACE_BOLD, 15, cx, top + 15, m->author_name, (int)strlen(m->author_name), COL_HEADER);
        const char *t = system_text(m);
        font_draw(c, FACE_REGULAR, 15, cx + w + 5, top + 15, t, (int)strlen(t), COL_MUTED);
        return;
    }
    if (!grouped)
        y += 16;
    if (m->is_reply)
    {
        sx_draw_line(c, x + 36, y + 18, x + 36, y + 10, 2, COL_FAINT);
        sx_draw_line(c, x + 36, y + 10, x + 66, y + 10, 2, COL_FAINT);
        int w = 0;
        if (m->reply_author && *m->reply_author)
        {
            char at[140];
            snprintf(at, sizeof(at), "@%s", m->reply_author);
            w = font_draw(c, FACE_BOLD, 13, cx, y + 14, at, (int)strlen(at), COL_MUTED) + 6;
        }
        char snippet[200];
        int k = 0;
        for (const char *p = m->reply_content; p && *p && k < 190; p++)
            snippet[k++] = *p == '\n' ? ' ' : *p;
        snippet[k] = '\0';
        font_draw_ellipsis(c, FACE_REGULAR, 13, cx + w, y + 14, snippet, content_w - w, COL_MUTED);
        y += 20;
    }
    if (!grouped)
    {
        draw_avatar(c, m->author_id, m->author_avatar, m->author_disc, x + 16, y + 2, AVATAR);
        uint32_t name_col = m->author_color ? (0xFF000000u | m->author_color) : COL_HEADER;
        int w = font_draw(c, FACE_BOLD, UI_NAME_PX, cx, y + 16, m->author_name, (int)strlen(m->author_name), name_col);
        if (m->author_bot)
        {
            sx_fill_rounded_rect(c, cx + w + 6, y + 3, 30, 16, 3, COL_ACCENT);
            text_center(c, FACE_BOLD, 10, sx_rect(cx + w + 6, y + 3, 30, 16), "BOT", COL_HEADER);
            w += 36;
        }
        char ts[48];
        format_time(m->timestamp, ts, sizeof(ts), true);
        font_draw(c, FACE_REGULAR, UI_SMALL_PX, cx + w + 8, y + 16, ts, (int)strlen(ts), COL_FAINT);
        y += 22;
    }
    else if (hovering(sx_rect(x, y, content_w + GUTTER, rich_height(m->layout) + 4)))
    {
        char ts[16];
        format_time(m->timestamp, ts, sizeof(ts), false);
        font_draw(c, FACE_REGULAR, 11, x + 22, y + 15, ts, (int)strlen(ts), COL_FAINT);
    }
    rich_draw(c, m->layout, cx, y);
    if (m->edited && m->layout && rich_height(m->layout) > 0)
    {
        const char *tag = "(edited)";
        font_draw(c, FACE_REGULAR, 11, cx + content_w - font_width(FACE_REGULAR, 11, tag, 8), y + rich_height(m->layout) - 5, tag, 8, COL_FAINT);
    }
    y += rich_height(m->layout);
    for (int i = 0; i < m->attachment_count; i++)
    {
        Attachment *a = &m->attachments[i];
        if (is_image(a))
        {
            int w, h;
            fit_box(a->width, a->height, content_w < 400 ? content_w : 400, 300, &w, &h);
            const char *src = a->proxy_url ? a->proxy_url : a->url;
            char url[1200];
            media_url(src ? src : "", w, h, a->content_type, url, sizeof(url));
            SxImage *img;
            if (y + h + 4 >= c->clip.y && y <= c->clip.y + c->clip.h && img_request(url, w, h, &img) == IMG_READY && img)
                img_draw(c, img, cx, y + 4, 6);
            else
                sx_fill_rounded_rect(c, cx, y + 4, w, h, 6, COL_SIDEBAR);
            y += h + 6;
        }
        else
        {
            int w = content_w < 400 ? content_w : 400;
            sx_fill_rounded_rect(c, cx, y + 4, w, 50, 6, COL_SIDEBAR);
            sx_fill_rounded_rect(c, cx + 12, y + 13, 24, 32, 3, COL_ACCENT);
            font_draw_ellipsis(c, FACE_REGULAR, 15, cx + 46, y + 26, a->filename, w - 60, COL_LINK);
            char size[48];
            if (a->size >= 1024 * 1024)
                snprintf(size, sizeof(size), "%.2f MB", a->size / (1024.0 * 1024.0));
            else
                snprintf(size, sizeof(size), "%.1f KB", a->size / 1024.0);
            font_draw(c, FACE_REGULAR, 12, cx + 46, y + 44, size, (int)strlen(size), COL_FAINT);
            y += 58;
        }
    }
    for (int i = 0; i < m->embed_count; i++)
    {
        draw_embed(c, m, i, cx, y + 4, content_w);
        y += embed_height(m, i, content_w) + 6;
    }
}

static bool group_with(const Message *prev, const Message *m)
{
    if (!prev || is_system(m) || is_system(prev) || m->is_reply)
        return false;
    if (prev->author_id != m->author_id)
        return false;
    return m->timestamp - prev->timestamp < 420;
}

static void draw_messages(SxCanvas *c)
{
    int x0 = GUILD_W + SIDEBAR_W;
    int w = U.w - x0;
    sx_fill_rect(c, x0, 0, w, U.h, COL_MAIN);

    Channel *ch = U.channel ? store_channel(U.channel) : NULL;

    sx_fill_rect(c, x0, HEADER_H - 1, w, 1, COL_DIVIDER);
    if (ch)
    {
        char name[128];
        store_channel_title(ch, name, sizeof(name));
        int hx = x0 + 16;
        if (ch->guild_id)
            draw_hash(c, hx, 14, COL_FAINT);
        else
            font_draw(c, FACE_BOLD, 18, hx, 31, "@", 1, COL_FAINT);
        hx += 28;
        int nw = font_draw_ellipsis(c, FACE_BOLD, 15, hx, 30, name, w - 200, COL_HEADER);
        if (ch->topic && *ch->topic)
        {
            sx_fill_rect(c, hx + nw + 12, 16, 1, 18, COL_SELECTED);
            char topic[200];
            int k = 0;
            for (const char *p = ch->topic; *p && k < 190; p++)
                topic[k++] = *p == '\n' ? ' ' : *p;
            topic[k] = '\0';
            font_draw_ellipsis(c, FACE_REGULAR, 13, hx + nw + 24, 30, topic, w - (hx - x0) - nw - 40, COL_MUTED);
        }
    }

    int input_y = U.h - INPUT_GAP - INPUT_H;
    SxRect area = sx_rect(x0, HEADER_H, w, input_y - 8 - HEADER_H);
    add_hit(HIT_MESSAGES_AREA, area, 0);

    if (!ch)
    {
        const char *msg = S.ready ? (U.guild ? "This server has no text channels you can see." : "Pick a conversation on the left.")
                                  : "Loading...";
        text_center(c, FACE_REGULAR, 15, area, msg, COL_MUTED);
        return;
    }

    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, area));
    int content_w = w - GUTTER - 48;
    if (content_w < 80)
        content_w = 80;

    int count = ch->message_count;
    int *heights = count ? malloc(sizeof(int) * (size_t)count) : NULL;
    bool *grouped = count ? malloc(sizeof(bool) * (size_t)count) : NULL;
    int total = 0;
    for (int i = 0; heights && grouped && i < count; i++)
    {
        grouped[i] = group_with(i ? ch->messages[i - 1] : NULL, ch->messages[i]);
        heights[i] = message_height(ch->messages[i], content_w, ch->guild_id, grouped[i]);
        total += heights[i];
    }
    int intro_h = ch->has_more && ch->message_count ? 48 : 110;
    total += intro_h + 16;

    Snowflake newest = count ? ch->messages[count - 1]->id : 0;
    if (U.msg_channel == ch->id && newest != U.msg_newest && U.msg_scroll > 0)
        U.msg_scroll += total - U.msg_total;
    U.msg_channel = ch->id;
    U.msg_newest = newest;
    U.msg_total = total;
    int max_scroll = total - area.h;
    if (max_scroll < 0)
        max_scroll = 0;
    if (U.msg_scroll > max_scroll)
        U.msg_scroll = max_scroll;
    if (U.msg_scroll < 0)
        U.msg_scroll = 0;

    int bottom = area.y + area.h - 16 + U.msg_scroll;
    int yb = bottom;
    for (int i = count - 1; heights && grouped && i >= 0; i--)
    {
        int top = yb - heights[i];
        if (top < area.y + area.h && yb > area.y)
            draw_message(c, ch->messages[i], x0, top, content_w, grouped[i]);
        yb = top;
    }
    int intro_top = yb - intro_h;
    if (intro_top + intro_h > area.y)
    {
        if (ch->has_more && ch->message_count)
        {
            text_center(c, FACE_REGULAR, 13, sx_rect(x0, intro_top, w, intro_h), ch->loading ? "Loading older messages..." : "", COL_MUTED);
        }
        else if (ch->loaded)
        {
            char name[128];
            store_channel_title(ch, name, sizeof(name));
            char line[200];
            snprintf(line, sizeof(line), ch->guild_id ? "Welcome to #%s!" : "%s", name);
            font_draw(c, FACE_BOLD, 26, x0 + 16, intro_top + 50, line, (int)strlen(line), COL_HEADER);
            const char *sub = ch->guild_id ? "This is the start of this channel." : "This is the beginning of your direct message history.";
            font_draw(c, FACE_REGULAR, 14, x0 + 16, intro_top + 78, sub, (int)strlen(sub), COL_MUTED);
        }
    }
    free(heights);
    free(grouped);

    if (!ch->loaded)
    {
        if (ch->load_failed)
        {
            text_center(c, FACE_REGULAR, 15, sx_rect(area.x, area.y + area.h / 2 - 30, area.w, 24), ch->error, COL_MUTED);
            SxRect btn = sx_rect(area.x + area.w / 2 - 50, area.y + area.h / 2, 100, 32);
            sx_fill_rounded_rect(c, btn.x, btn.y, btn.w, btn.h, 4, hovering(btn) ? COL_ACCENT_HI : COL_ACCENT);
            text_center(c, FACE_BOLD, 14, btn, "Retry", COL_HEADER);
            add_hit(HIT_RETRY_LOAD, btn, ch->id);
        }
        else
        {
            text_center(c, FACE_REGULAR, 15, area, "Loading messages...", COL_MUTED);
        }
    }

    if (total > area.h)
    {
        int track = area.h - 8;
        int thumb = track * area.h / total;
        if (thumb < 24)
            thumb = 24;
        int pos = (int)((long)(track - thumb) * (max_scroll - U.msg_scroll) / (max_scroll ? max_scroll : 1));
        sx_fill_rounded_rect(c, U.w - 12, area.y + 4 + pos, 8, thumb, 4, COL_GUILDBAR);
    }
    sx_canvas_set_clip(c, old);

    if (ch->loaded && ch->has_more && !ch->loading && U.msg_scroll + area.h > total - 600)
        load_messages(ch, true);

    SxRect in = sx_rect(x0 + 16, input_y, w - 32, INPUT_H);
    sx_fill_rounded_rect(c, in.x, in.y, in.w, in.h, 8, COL_INPUT);
    char placeholder[160];
    char name[128];
    store_channel_title(ch, name, sizeof(name));
    snprintf(placeholder, sizeof(placeholder), "Message %s%s", ch->guild_id ? "#" : "@", name);
    draw_field(c, &U.input, in, U.focused, false, placeholder, 15);
    add_hit(HIT_INPUT, in, 0);

    if (U.status[0] && uptime_ms() - U.status_at < 5000)
    {
        font_draw_ellipsis(c, FACE_REGULAR, 12, in.x + 4, U.h - 6, U.status, in.w, COL_RED);
    }
}

static void draw_qr_panel(SxCanvas *c, SxRect r)
{
    RaState st = ra_state();
    SxRect box = sx_rect(r.x + (r.w - 176) / 2, r.y, 176, 176);
    sx_fill_rounded_rect(c, box.x, box.y, box.w, box.h, 8, SX_RGB(0xFF, 0xFF, 0xFF));

    if (st == RA_WAITING && ra_fingerprint()[0])
    {
        char url[200];
        snprintf(url, sizeof(url), "https://discord.com/ra/%s", ra_fingerprint());
        if (strcmp(url, U.qr_for) != 0)
        {
            qr_free(&U.qr);
            qr_encode(url, &U.qr);
            snprintf(U.qr_for, sizeof(U.qr_for), "%s", url);
        }
        if (U.qr.size)
        {
            int cell = 160 / U.qr.size;
            int size = cell * U.qr.size;
            int ox = box.x + (box.w - size) / 2;
            int oy = box.y + (box.h - size) / 2;
            for (int y = 0; y < U.qr.size; y++)
                for (int x = 0; x < U.qr.size; x++)
                    if (qr_module(&U.qr, x, y))
                        sx_fill_rect(c, ox + x * cell, oy + y * cell, cell, cell, SX_RGB(0, 0, 0));
        }
    }
    else if (st == RA_SCANNED || st == RA_DONE)
    {
        SxImage *img;
        const char *av = ra_user_avatar();
        if (av[0] && img_request(av, 96, 96, &img) == IMG_READY && img)
            img_draw_circle(c, img, box.x + 40, box.y + 40, 96);
        else
            sx_fill_circle(c, box.x + 88, box.y + 88, 48, COL_ACCENT);
    }
    else if (st == RA_FAILED)
    {
        text_center(c, FACE_BOLD, 14, sx_rect(box.x, box.y + 60, box.w, 24), "Click to refresh", SX_RGB(0x31, 0x33, 0x38));
        add_hit(HIT_QR_RETRY, box, 0);
    }
    else
    {
        text_center(c, FACE_REGULAR, 14, box, "Preparing...", SX_RGB(0x5C, 0x5E, 0x66));
    }

    int y = box.y + box.h + 28;
    if (st == RA_SCANNED || st == RA_DONE)
    {
        text_center(c, FACE_BOLD, 22, sx_rect(r.x, y - 8, r.w, 30), "Check your phone!", COL_HEADER);
        char who[160];
        snprintf(who, sizeof(who), "Logging in as %s", ra_user());
        text_wrap(c, FACE_REGULAR, 14, r.x, y + 28, r.w, who, COL_MUTED, true);
    }
    else
    {
        text_center(c, FACE_BOLD, 20, sx_rect(r.x, y - 8, r.w, 30), "Log in with QR Code", COL_HEADER);
        const char *sub = st == RA_FAILED ? ra_error() : "Scan this with the Discord mobile app to log in instantly.";
        text_wrap(c, FACE_REGULAR, 14, r.x, y + 28, r.w, sub, COL_MUTED, true);
    }
}

static void draw_login(SxCanvas *c)
{
    sx_fill_gradient(c, 0, 0, U.w, U.h, SX_RGB(0x40, 0x4E, 0xED), SX_RGB(0x23, 0x27, 0x6E));
    int cw = 780, ch = 420;
    if (cw > U.w - 32)
        cw = U.w - 32;
    int cx = (U.w - cw) / 2, cy = (U.h - ch) / 2;
    sx_fill_rounded_rect(c, cx, cy, cw, ch, 8, COL_MAIN);

    int lw = cw - 64 - 296;
    int lx = cx + 32;
    text_center(c, FACE_BOLD, 24, sx_rect(lx, cy + 32, lw, 32), "Welcome back!", COL_HEADER);
    text_center(c, FACE_REGULAR, 15, sx_rect(lx, cy + 66, lw, 22), "We're so excited to see you again!", COL_MUTED);

    int fy = cy + 120;
    font_draw(c, FACE_BOLD, 12, lx, fy, "TOKEN", 5, COL_MUTED);
    font_draw(c, FACE_REGULAR, 12, lx + 48, fy, "*", 1, COL_RED);
    SxRect field = sx_rect(lx, fy + 10, lw, 40);
    sx_fill_rounded_rect(c, field.x, field.y, field.w, field.h, 4, COL_GUILDBAR);
    draw_field(c, &U.token, field, U.token_focus && U.focused, true, NULL, 15);
    add_hit(HIT_TOKEN, field, 0);

    SxRect btn = sx_rect(lx, fy + 66, lw, 44);
    sx_fill_rounded_rect(c, btn.x, btn.y, btn.w, btn.h, 4, hovering(btn) ? COL_ACCENT_HI : COL_ACCENT);
    text_center(c, FACE_BOLD, 15, btn, "Log In", COL_HEADER);
    add_hit(HIT_LOGIN, btn, 0);

    int ny = fy + 128;
    if (U.login_error[0])
        ny += text_wrap(c, FACE_REGULAR, 13, lx, ny, lw, U.login_error, COL_RED, false) + 8;
    ny += text_wrap(c, FACE_REGULAR, 12, lx, ny, lw,
                    "Bot accounts: type \"Bot \" before the token. The token is saved in /config/discord/token, "
                    "so you can also place it there.",
                    COL_FAINT, false) + 6;
    text_wrap(c, FACE_REGULAR, 12, lx, ny, lw,
              "Third-party clients are against Discord's Terms of Service. Use at your own risk.", COL_FAINT, false);

    sx_fill_rect(c, cx + cw - 280 - 16, cy + 32, 1, ch - 64, COL_SELECTED);
    draw_qr_panel(c, sx_rect(cx + cw - 280, cy + 40, 248, ch - 80));
}

static void draw_connecting(SxCanvas *c)
{
    sx_fill_rect(c, 0, 0, U.w, U.h, COL_MAIN);
    int phase = (int)(uptime_ms() / 300 % 4);
    char dots[8] = "";
    for (int i = 0; i < phase; i++)
        strcat(dots, ".");
    char line[64];
    snprintf(line, sizeof(line), "Connecting to Discord%s", dots);
    sx_fill_circle(c, U.w / 2.0f, U.h / 2.0f - 50, 32, COL_ACCENT);
    text_center(c, FACE_BOLD, 18, sx_rect(0, U.h / 2 - 10, U.w, 30), line, COL_HEADER);
    text_center(c, FACE_REGULAR, 13, sx_rect(0, U.h / 2 + 22, U.w, 20), "Loading your servers and messages.", COL_MUTED);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(U.win);
    U.w = c->width;
    U.h = c->height;
    U.hit_count = 0;
    sx_canvas_reset_clip(c);
    switch (U.screen)
    {
        case SCREEN_LOGIN:
            draw_login(c);
            break;
        case SCREEN_CONNECTING:
            draw_connecting(c);
            break;
        case SCREEN_MAIN:
            draw_messages(c);
            draw_sidebar(c);
            draw_guild_bar(c);
            break;
    }
    sx_window_commit(U.win);
    U.dirty = false;
}

static void on_click(int x, int y)
{
    Hit *h = hit_at(x, y);
    if (U.screen == SCREEN_LOGIN)
    {
        U.token_focus = h && h->kind == HIT_TOKEN;
        if (h && h->kind == HIT_LOGIN)
            begin_login(U.token.text);
        else if (h && h->kind == HIT_QR_RETRY)
            U.ra_start_pending = true;
        U.dirty = true;
        return;
    }
    if (!h)
        return;
    switch (h->kind)
    {
        case HIT_HOME:
            select_guild(0);
            break;
        case HIT_GUILD:
            select_guild(h->id);
            break;
        case HIT_CHANNEL:
            select_channel(store_channel(h->id));
            break;
        case HIT_LOGOUT:
            logout();
            break;
        case HIT_RETRY_LOAD:
        {
            Channel *ch = store_channel(h->id);
            if (ch)
                load_messages(ch, false);
            break;
        }
        default:
            break;
    }
    U.dirty = true;
}

static void on_wheel(int x, int y, int wheel)
{
    Hit *h = NULL;
    for (int i = 0; i < U.hit_count; i++)
        if ((U.hits[i].kind == HIT_GUILD_AREA || U.hits[i].kind == HIT_SIDEBAR_AREA || U.hits[i].kind == HIT_MESSAGES_AREA) &&
            sx_rect_contains(U.hits[i].r, x, y))
            h = &U.hits[i];
    if (!h)
        return;
    if (h->kind == HIT_GUILD_AREA)
    {
        U.guild_scroll -= wheel * 56;
        if (U.guild_scroll < 0)
            U.guild_scroll = 0;
    }
    else if (h->kind == HIT_SIDEBAR_AREA)
    {
        U.side_scroll -= wheel * 40;
        if (U.side_scroll < 0)
            U.side_scroll = 0;
    }
    else
    {
        U.msg_scroll += wheel * 60;
    }
    U.dirty = true;
}

static void on_key(const SxEvent *ev)
{
    if (U.screen == SCREEN_LOGIN)
    {
        if (field_key(&U.token, ev))
            begin_login(U.token.text);
        U.dirty = true;
        return;
    }
    if (U.screen != SCREEN_MAIN)
        return;
    int page = U.h - HEADER_H - INPUT_H - INPUT_GAP - 80;
    switch (ev->key)
    {
        case SX_KEY_PAGE_UP:
            U.msg_scroll += page;
            break;
        case SX_KEY_PAGE_DOWN:
            U.msg_scroll -= page;
            break;
        default:
            if (field_key(&U.input, ev))
                send_message();
            break;
    }
    U.caret_on = true;
    U.caret_at = uptime_ms();
    U.dirty = true;
}

static void handle_event(const SxEvent *ev)
{
    switch (ev->type)
    {
        case SX_EVENT_RESIZE:
            U.dirty = true;
            break;
        case SX_EVENT_FOCUS_IN:
        case SX_EVENT_FOCUS_OUT:
            U.focused = ev->type == SX_EVENT_FOCUS_IN;
            U.dirty = true;
            break;
        case SX_EVENT_MOUSE_MOVE:
            U.hover_x = ev->x;
            U.hover_y = ev->y;
            U.dirty = true;
            break;
        case SX_EVENT_MOUSE_LEAVE:
            U.hover_x = U.hover_y = -1;
            U.dirty = true;
            break;
        case SX_EVENT_MOUSE_DOWN:
            if (ev->button == SX_BUTTON_LEFT)
                on_click(ev->x, ev->y);
            break;
        case SX_EVENT_MOUSE_WHEEL:
            on_wheel(ev->x, ev->y, ev->wheel);
            break;
        case SX_EVENT_KEY_DOWN:
            on_key(ev);
            break;
        default:
            break;
    }
}

static void update_state(void)
{
    if (U.screen == SCREEN_LOGIN)
    {
        if (ra_state() == RA_DONE && ra_token()[0])
        {
            char token[256];
            snprintf(token, sizeof(token), "%s", ra_token());
            begin_login(token);
        }
        return;
    }
    GwState gs = gw_state();
    if (gs == GW_FAILED)
    {
        show_login(gw_error());
        return;
    }
    if (U.screen == SCREEN_CONNECTING && gs == GW_READY && S.ready)
    {
        save_token(auth_token);
        U.screen = SCREEN_MAIN;
        U.focused = true;
        select_guild(0);
        U.dirty = true;
    }
    if (U.screen == SCREEN_MAIN && U.channel && !store_channel(U.channel))
        select_guild(U.guild && store_guild(U.guild) ? U.guild : 0);
    if (U.screen == SCREEN_MAIN && U.guild && !store_guild(U.guild))
        select_guild(0);
}

static void parse_args(int argc, char **argv, char *token, size_t token_size)
{
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const char *k = argv[i], *v = argv[i + 1];
        if (strcmp(k, "--api") == 0)
            snprintf(api_base, sizeof(api_base), "%s", v);
        else if (strcmp(k, "--gateway") == 0)
            snprintf(gateway_url, sizeof(gateway_url), "%s", v);
        else if (strcmp(k, "--cdn") == 0)
            snprintf(cdn_base, sizeof(cdn_base), "%s", v);
        else if (strcmp(k, "--token") == 0)
            snprintf(token, token_size, "%s", v);
    }
}

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("discord: cannot connect to the display server\n");
        return 1;
    }
    if (!font_init())
    {
        printf("discord: fonts missing from /share/fonts\n");
        return 1;
    }
    U.win = sx_window_create("Discord", 1120, 720, SX_WINDOW_RESIZABLE);
    if (!U.win)
        return 1;
    U.hover_x = U.hover_y = -1;
    U.focused = true;
    U.caret_on = true;
    U.caret_at = uptime_ms();

    char token[256] = "";
    parse_args(argc, argv, token, sizeof(token));
    if (token[0] || load_token(token, sizeof(token)))
        begin_login(token);
    else
        show_login(NULL);

    draw();
    for (;;)
    {
        struct pollfd fds[48];
        int n = 0;
        fds[n].fd = sx_fd();
        fds[n].events = POLLIN;
        fds[n].revents = 0;
        n++;
        n += http_pollfds(fds + n, 40 - n);
        n += gw_pollfds(fds + n, 48 - n);
        n += ra_pollfds(fds + n, 48 - n);

        uint64_t now = uptime_ms();
        int64_t timeout = 530 - (int64_t)(now - U.caret_at);
        if (timeout < 0)
            timeout = 0;
        uint64_t deadlines[2] = { gw_next_deadline(), ra_next_deadline() };
        for (int i = 0; i < 2; i++)
            if (deadlines[i])
            {
                int64_t d = deadlines[i] > now ? (int64_t)(deadlines[i] - now) : 0;
                if (d < timeout)
                    timeout = d;
            }
        if (U.screen == SCREEN_CONNECTING && timeout > 100)
            timeout = 100;
        if (U.dirty || U.ra_start_pending || http_busy() || gw_has_buffered() || ra_has_buffered())
            timeout = http_busy() && !U.dirty ? 15 : 0;
        poll(fds, (uint64_t)n, timeout);

        SxEvent ev;
        int rc;
        bool quit = false;
        while ((rc = sx_next_event(&ev, 0)) > 0)
        {
            if (ev.type == SX_EVENT_CLOSE || ev.type == SX_EVENT_DISCONNECTED)
            {
                quit = true;
                break;
            }
            handle_event(&ev);
        }
        if (quit || rc < 0)
            break;

        http_pump();
        gw_step();
        ra_step();
        update_state();

        now = uptime_ms();
        if (now - U.caret_at >= 530)
        {
            U.caret_on = !U.caret_on;
            U.caret_at = now;
            if (U.screen != SCREEN_CONNECTING)
                U.dirty = true;
        }
        if (U.screen == SCREEN_CONNECTING)
            U.dirty = true;
        if (S.version != U.seen_version)
        {
            U.seen_version = S.version;
            U.dirty = true;
        }
        if (img_take_changed())
            U.dirty = true;
        if (U.dirty)
            draw();
        if (U.ra_start_pending)
        {
            U.ra_start_pending = false;
            if (U.screen == SCREEN_LOGIN)
            {
                ra_start();
                U.dirty = true;
            }
        }
    }

    gw_stop();
    ra_stop();
    sx_window_destroy(U.win);
    sx_disconnect();
    return 0;
}
