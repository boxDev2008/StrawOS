#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "discord.h"

#define TOKEN_DIR   "/config/discord"
#define TOKEN_PATH  "/config/discord/token"
#define BROWSER     "/bin/netsurf.elf"
#define SAVE_DIR    "/pictures/discord"

#define GUILD_W     72
#define SIDEBAR_W   240
#define HEADER_H    48
#define USER_H      52
#define VOICE_H     98
#define DAY_H       36
#define VOICE_ROW   28
#define INPUT_H     44
#define INPUT_GAP   24
#define AVATAR      40
#define GUTTER      72
#define MAX_HITS    1024
#define POOL_SIZE   65536
#define MAX_MEDIA   128

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
    HIT_VOICE,
    HIT_CALL,
    HIT_VC_MUTE,
    HIT_VC_DEAF,
    HIT_VC_LEAVE,
    HIT_VC_STREAM,
    HIT_WATCH,
    HIT_LINK,
    HIT_IMAGE,
    HIT_VIEW_BG,
    HIT_VIEW_IMG,
    HIT_VIEW_CLOSE,
    HIT_VIEW_SAVE,
    HIT_VIEW_OPEN,
}
HitKind;

typedef struct
{
    int  url;
    int  proxy;
    int  name;
    int  ctype;
    int  w, h;
    long size;
}
MediaRef;

typedef struct
{
    bool  open;
    char *url;
    char *proxy;
    char *name;
    char *ctype;
    int   w, h;
    long  size;
}
Viewer;

typedef enum
{
    TIP_RIGHT,
    TIP_ABOVE,
    TIP_BELOW,
}
TipSide;

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

    char        pool[POOL_SIZE];
    int         pool_len;
    MediaRef    media[MAX_MEDIA];
    int         media_count;
    Viewer      view;

    char        tip[128];
    SxRect      tip_r;
    TipSide     tip_side;

    QrCode      qr;
    char        qr_for[160];
    bool        ra_start_pending;

    char        login_error[192];
    char        status[192];
    bool        status_ok;
    uint64_t    status_at;

    bool        caret_on;
    uint64_t    caret_at;
    int         seen_version;
}
U;

static void set_status(const char *msg)
{
    snprintf(U.status, sizeof(U.status), "%s", msg);
    U.status_ok = false;
    U.status_at = uptime_ms();
    U.dirty = true;
}

static void set_status_ok(const char *msg)
{
    set_status(msg);
    U.status_ok = true;
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

static int pool_add(const char *s, int len)
{
    if (!s)
        return -1;
    if (len < 0)
        len = (int)strlen(s);
    if (U.pool_len + len + 1 > POOL_SIZE)
        return -1;
    int off = U.pool_len;
    memcpy(U.pool + off, s, (size_t)len);
    U.pool[off + len] = '\0';
    U.pool_len += len + 1;
    return off;
}

static const char *pool_get(int off)
{
    return off >= 0 && off < U.pool_len ? U.pool + off : NULL;
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
    if ((ev->mods & SX_MOD_CTRL) && (ev->key == 'c' || ev->key == 'x'))
    {
        sx_clipboard_set(f->text, (size_t)f->len);
        if (ev->key == 'x')
        {
            f->len = f->cursor = 0;
            f->text[0] = '\0';
        }
        return false;
    }
    if ((ev->mods & SX_MOD_CTRL) && ev->key == 'v')
    {
        size_t n;
        char *text = sx_clipboard_get(&n);
        if (!text)
            return false;
        int room = (int)sizeof(f->text) - 1 - f->len;
        int len = 0;
        size_t i = 0;
        for (; i < n && len < room; i++)
        {
            unsigned char c = (unsigned char)text[i];
            if (c == '\n' || c == '\t')
                text[len++] = ' ';
            else if (c >= 32 && c != 127)
                text[len++] = (char)c;
        }
        if (i < n)
        {
            while (len > 0 && ((unsigned char)text[len - 1] & 0xC0) == 0x80)
                len--;
            if (len > 0 && ((unsigned char)text[len - 1] & 0xC0) == 0xC0)
                len--;
        }
        field_insert(f, text, len);
        free(text);
        return false;
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

static void viewer_close(void);

static void show_login(const char *error)
{
    viewer_close();
    vc_reset();
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

static bool open_url(const char *url)
{
    if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0))
        return false;
    const char *argv[] = { BROWSER, url, NULL };
    if (!spawn(BROWSER, argv))
    {
        set_status("Couldn't start NetSurf.");
        return false;
    }
    set_status_ok("Opened link in NetSurf.");
    return true;
}

static void viewer_close(void)
{
    free(U.view.url);
    free(U.view.proxy);
    free(U.view.name);
    free(U.view.ctype);
    memset(&U.view, 0, sizeof(U.view));
    U.dirty = true;
}

static void viewer_open(const MediaRef *m)
{
    viewer_close();
    const char *url = pool_get(m->url);
    const char *proxy = pool_get(m->proxy);
    if (!url && !proxy)
        return;
    U.view.url = xstrdup(url ? url : proxy);
    U.view.proxy = xstrdup(proxy ? proxy : url);
    U.view.name = xstrdup(pool_get(m->name) ? pool_get(m->name) : "image");
    U.view.ctype = xstrdup(pool_get(m->ctype));
    U.view.w = m->w;
    U.view.h = m->h;
    U.view.size = m->size;
    U.view.open = true;
    U.dirty = true;
}

static void save_path(const char *name, char *out, size_t size)
{
    char clean[96];
    int n = 0;
    for (const char *p = name; *p && n < (int)sizeof(clean) - 1; p++)
    {
        char ch = *p;
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
                  ch == '-' || ch == '_';
        clean[n++] = ok ? ch : '_';
    }
    clean[n] = '\0';
    if (!n || clean[0] == '.')
        snprintf(clean, sizeof(clean), "image.png");
    char *dot = strrchr(clean, '.');
    char ext[24] = "";
    if (dot && strlen(dot) < sizeof(ext))
    {
        snprintf(ext, sizeof(ext), "%s", dot);
        *dot = '\0';
    }
    struct stat st;
    snprintf(out, size, "%s/%s%s", SAVE_DIR, clean, ext);
    for (int i = 1; i < 1000 && stat(out, &st) == 0; i++)
        snprintf(out, size, "%s/%s (%d)%s", SAVE_DIR, clean, i, ext);
}

typedef struct
{
    char path[256];
}
SaveCtx;

static void on_saved(int status, const uint8_t *body, size_t len, void *user)
{
    SaveCtx *ctx = user;
    char msg[320];
    FILE *f = status >= 200 && status < 300 ? fopen(ctx->path, "wb") : NULL;
    if (f && fwrite(body, 1, len, f) == len)
    {
        snprintf(msg, sizeof(msg), "Saved to %s", ctx->path);
        fclose(f);
        set_status_ok(msg);
    }
    else
    {
        if (f)
        {
            fclose(f);
            unlink(ctx->path);
        }
        if (status >= 200 && status < 300)
            snprintf(msg, sizeof(msg), "Couldn't write %s", ctx->path);
        else
            snprintf(msg, sizeof(msg), "Download failed (HTTP %d).", status);
        set_status(msg);
    }
    free(ctx);
}

static void download_view(void)
{
    if (!U.view.open)
        return;
    SaveCtx *ctx = malloc(sizeof(SaveCtx));
    if (!ctx)
        return;
    mkdir("/pictures", 0755);
    mkdir(SAVE_DIR, 0755);
    save_path(U.view.name, ctx->path, sizeof(ctx->path));
    if (http_request("GET", U.view.url, NULL, NULL, 0, on_saved, ctx))
        set_status_ok("Downloading...");
    else
    {
        free(ctx);
        set_status("Couldn't start the download.");
    }
}

static void url_basename(const char *url, char *out, size_t size)
{
    const char *end = url + strcspn(url, "?#");
    const char *start = end;
    while (start > url && start[-1] != '/')
        start--;
    int n = (int)(end - start);
    if (n <= 0)
        snprintf(out, size, "image.png");
    else
        snprintf(out, size, "%.*s", n, start);
}

static void add_media(SxCanvas *c, SxRect r, const char *url, const char *proxy, const char *name, const char *ctype,
                      int w, int h, long size)
{
    SxRect vis = sx_rect_intersect(r, c->clip);
    if (sx_rect_empty(vis) || U.media_count >= MAX_MEDIA)
        return;
    MediaRef *m = &U.media[U.media_count];
    m->url = pool_add(url, -1);
    m->proxy = pool_add(proxy, -1);
    m->name = pool_add(name, -1);
    m->ctype = pool_add(ctype, -1);
    m->w = w;
    m->h = h;
    m->size = size;
    if (m->url < 0 && m->proxy < 0)
        return;
    add_hit(HIT_IMAGE, vis, (Snowflake)U.media_count++);
}

static void link_hit(SxRect r, const char *url, int len, void *user)
{
    SxCanvas *c = user;
    SxRect vis = sx_rect_intersect(r, c->clip);
    if (sx_rect_empty(vis))
        return;
    int off = pool_add(url, len);
    if (off < 0)
        return;
    add_hit(HIT_LINK, vis, (Snowflake)off);
    if (hovering(vis))
        sx_fill_rect(c, r.x + 1, r.y + font_ascent(FACE_REGULAR, UI_TEXT_PX) + 3, r.w - 2, 1, COL_LINK);
}

static void tooltip(SxRect anchor, const char *text, TipSide side)
{
    if (!hovering(anchor) || !text)
        return;
    snprintf(U.tip, sizeof(U.tip), "%s", text);
    U.tip_r = anchor;
    U.tip_side = side;
}

static void draw_tooltip(SxCanvas *c)
{
    if (!U.tip[0])
        return;
    int len = (int)strlen(U.tip);
    int tw = font_width(FACE_BOLD, 13, U.tip, len) + 22;
    int th = 30;
    if (tw > U.w - 16)
        tw = U.w - 16;
    SxRect a = U.tip_r;
    int x, y;
    if (U.tip_side == TIP_RIGHT)
    {
        x = a.x + a.w + 10;
        y = a.y + (a.h - th) / 2;
    }
    else if (U.tip_side == TIP_ABOVE)
    {
        x = a.x + (a.w - tw) / 2;
        y = a.y - th - 8;
    }
    else
    {
        x = a.x + (a.w - tw) / 2;
        y = a.y + a.h + 8;
    }
    int bx = x < 8 ? 8 : x + tw > U.w - 8 ? U.w - 8 - tw : x;
    int by = y < 4 ? 4 : y + th > U.h - 4 ? U.h - 4 - th : y;
    sx_fill_rounded_rect(c, bx - 1, by + 1, tw + 2, th + 3, 7, COL_SHADOW);
    sx_fill_rounded_rect(c, bx, by, tw, th, 6, COL_TOOLTIP);
    if (U.tip_side == TIP_RIGHT)
    {
        int cy = by + th / 2;
        for (int i = 0; i < 5; i++)
            sx_fill_rect(c, bx - 5 + i, cy - i, 1, i * 2 + 1, COL_TOOLTIP);
    }
    else
    {
        int cx = a.x + a.w / 2;
        if (cx < bx + 10)
            cx = bx + 10;
        if (cx > bx + tw - 10)
            cx = bx + tw - 10;
        for (int i = 0; i < 5; i++)
        {
            if (U.tip_side == TIP_ABOVE)
                sx_fill_rect(c, cx - i, by + th + 4 - i, i * 2 + 1, 1, COL_TOOLTIP);
            else
                sx_fill_rect(c, cx - i, by - 5 + i, i * 2 + 1, 1, COL_TOOLTIP);
        }
    }
    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, sx_rect(bx + 8, by, tw - 16, th)));
    text_center(c, FACE_BOLD, 13, sx_rect(bx, by, tw, th), U.tip, COL_HEADER);
    sx_canvas_set_clip(c, old);
}

static void icon_button(SxCanvas *c, SxRect r, IconId icon, int size, uint32_t fg, uint32_t hover_bg, HitKind kind,
                        Snowflake id, const char *tip, TipSide side)
{
    if (hovering(r))
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, hover_bg);
    icon_draw(c, icon, r.x + (r.w - size) / 2, r.y + (r.h - size) / 2, size, fg);
    add_hit(kind, r, id);
    tooltip(r, tip, side);
}

static void drop_shadow(SxCanvas *c, int x, int y, int w)
{
    sx_fill_gradient(c, x, y, w, 5, COL_SHADOW, COL_SHADOW & 0x00FFFFFFu);
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

static void draw_pill(SxCanvas *c, int y, bool selected, bool hovered)
{
    if (selected)
        sx_fill_rounded_rect(c, -4, y + 4, 8, 40, 4, COL_HEADER);
    else if (hovered)
        sx_fill_rounded_rect(c, -4, y + 14, 8, 20, 4, COL_HEADER);
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
    bool lit = sel || hov;
    sx_fill_rounded_rect(c, home.x, home.y, 48, 48, lit ? 16 : 24, lit ? COL_ACCENT : COL_MAIN);
    icon_draw(c, ICON_HOME, home.x + 11, home.y + 11, 26, lit ? COL_ON_ACCENT : COL_HEADER);
    draw_pill(c, home.y, sel, hov);
    add_hit(HIT_HOME, home, 0);
    tooltip(home, "Direct Messages", TIP_RIGHT);
    y += 58;
    sx_fill_rounded_rect(c, 24, y, 24, 2, 1, sx_mix(COL_GUILDBAR, COL_MUTED, 70));
    y += 10;

    for (int i = 0; i < S.guild_count; i++)
    {
        Guild *g = S.guilds[i];
        SxRect r = sx_rect(12, y, 48, 48);
        sel = U.guild == g->id;
        hov = hovering(r);
        lit = sel || hov;
        bool visible = r.y + 48 >= 0 && r.y <= U.h;
        bool drawn = false;
        if (g->icon && visible)
        {
            char url[300];
            snprintf(url, sizeof(url), "%s/icons/%llu/%s.png?size=96", cdn_base, (unsigned long long)g->id, g->icon);
            SxImage *img;
            if (img_request(url, 48, 48, &img) == IMG_READY && img)
            {
                img_draw(c, img, r.x, r.y, lit ? 16 : 24);
                drawn = true;
            }
        }
        if (!drawn && visible)
        {
            sx_fill_rounded_rect(c, r.x, r.y, 48, 48, lit ? 16 : 24, lit ? COL_ACCENT : COL_MAIN);
            draw_initials(c, g->name, r, lit ? COL_ON_ACCENT : COL_HEADER);
        }
        draw_pill(c, r.y, sel, hov);
        add_hit(HIT_GUILD, r, g->id);
        tooltip(r, g->name, TIP_RIGHT);
        y += 56;
    }
    int content = y + U.guild_scroll + 12;
    if (U.guild_scroll > content - U.h)
        U.guild_scroll = content - U.h > 0 ? content - U.h : 0;
    sx_canvas_set_clip(c, old);
}

static int draw_live_badge(SxCanvas *c, int right, int y, bool watching)
{
    const char *text = watching ? "WATCHING" : "LIVE";
    int len = (int)strlen(text);
    int w = font_width(FACE_BOLD, 10, text, len) + 10;
    uint32_t bg = watching ? COL_ACCENT : COL_RED;
    sx_fill_rounded_rect(c, right - w, y + 7, w, 15, 8, bg);
    font_draw(c, FACE_BOLD, 10, right - w + 5, y + 18, text, len, watching ? COL_ON_ACCENT : SX_RGB(0xFF, 0xFF, 0xFF));
    return w + 4;
}

static const char *display_name(Snowflake id)
{
    if (id == S.me.id)
        return S.me.global_name && *S.me.global_name ? S.me.global_name : S.me.username ? S.me.username : "";
    User *u = store_user(id);
    if (u && u->global_name && *u->global_name)
        return u->global_name;
    if (u && u->username)
        return u->username;
    const char *name = store_user_name(id);
    return name ? name : "Unknown user";
}

static int draw_voice_members(SxCanvas *c, Channel *ch, int x, int y, int w)
{
    const VoiceState *members[64];
    int n = store_voice_members(ch->id, members, 64);
    bool ours = vc_channel() == ch->id && vc_state() == VC_CONNECTED;
    for (int i = 0; i < n; i++)
    {
        const VoiceState *v = members[i];
        User *u = store_user(v->user_id);
        bool speaking = ours && vc_speaking(v->user_id);
        bool watchable = v->streaming && ours && v->user_id != S.me.id;
        SxRect row = sx_rect(x - 6, y, w + 6, VOICE_ROW);
        bool hov = hovering(row);
        if (watchable)
        {
            if (hov)
                sx_fill_rounded_rect(c, row.x, row.y + 1, row.w, row.h - 2, 6, COL_HOVER);
            add_hit(HIT_WATCH, row, v->user_id);
            tooltip(row, vc_watching() == v->user_id ? "Watching stream" : "Watch stream", TIP_RIGHT);
        }
        draw_avatar(c, v->user_id, u ? u->avatar : NULL, u ? u->discriminator : NULL, x + 2, y + 4, 20);
        if (speaking)
            sx_draw_circle(c, x + 12, y + 14, 11.5f, 2.0f, COL_GREEN);
        int right = x + w;
        if (v->streaming)
            right -= draw_live_badge(c, right, y, vc_watching() == v->user_id);
        if (v->deafened || v->muted)
        {
            right -= 18;
            icon_draw(c, v->deafened ? ICON_HEADPHONES_OFF : ICON_MIC_OFF, right, y + 6, 16, COL_MUTED);
            right -= 2;
        }
        font_draw_ellipsis(c, FACE_REGULAR, 14, x + 30, y + 19, display_name(v->user_id), right - x - 34,
                           speaking ? COL_HEADER : watchable && hov ? COL_TEXT : COL_MUTED);
        y += VOICE_ROW;
    }
    return n * VOICE_ROW;
}

static void draw_voice_panel(SxCanvas *c, int x0, int vy)
{
    sx_fill_rect(c, x0, vy, SIDEBAR_W, VOICE_H, COL_PANEL);
    sx_fill_rect(c, x0, vy, SIDEBAR_W, 1, COL_DIVIDER);
    sx_fill_rect(c, x0 + 10, vy + VOICE_H - 1, SIDEBAR_W - 20, 1, COL_DIVIDER);
    VcState st = vc_state();
    StreamState ss = vc_stream_state();
    const char *status;
    uint32_t color;
    switch (st)
    {
        case VC_CONNECTED:
            status = ss == SS_LIVE ? "Screen Live" : ss == SS_STARTING ? "Starting Stream..." : "Voice Connected";
            color = ss == SS_LIVE ? COL_RED : ss == SS_STARTING ? COL_YELLOW : COL_GREEN;
            break;
        case VC_FAILED:
            status = "Voice Disconnected";
            color = COL_RED;
            break;
        default:
            status = "Connecting...";
            color = COL_YELLOW;
            break;
    }
    icon_draw(c, ICON_SIGNAL, x0 + 12, vy + 12, 18, color);
    font_draw_ellipsis(c, FACE_BOLD, 14, x0 + 36, vy + 26, status, SIDEBAR_W - 92, color);

    char sub[160];
    bool lock = false;
    Channel *ch = store_channel(vc_channel());
    if (st == VC_FAILED && vc_error()[0])
        snprintf(sub, sizeof(sub), "%s", vc_error());
    else if (st == VC_CONNECTED && ss == SS_OFF && vc_stream_error()[0])
        snprintf(sub, sizeof(sub), "%s", vc_stream_error());
    else if (vc_watching())
        snprintf(sub, sizeof(sub), "%s %s", vc_watch_state() == SS_LIVE ? "Watching" : "Opening stream of",
                 display_name(vc_watching()));
    else if (ss == SS_LIVE)
    {
        int n = vc_stream_viewers();
        if (n == 0)
            snprintf(sub, sizeof(sub), "No one is watching yet");
        else
            snprintf(sub, sizeof(sub), "%d watching", n);
    }
    else if (ch)
    {
        char name[128];
        store_channel_title(ch, name, sizeof(name));
        Guild *g = ch->guild_id ? store_guild(ch->guild_id) : NULL;
        snprintf(sub, sizeof(sub), "%s%s%s", name, g ? " / " : "", g ? g->name : "");
        lock = vc_encrypted();
    }
    else
        sub[0] = '\0';
    int sx = x0 + 12;
    if (lock)
    {
        icon_draw(c, ICON_LOCK, sx, vy + 33, 13, COL_GREEN);
        sx += 17;
    }
    font_draw_ellipsis(c, FACE_REGULAR, 12, sx, vy + 44, sub, x0 + SIDEBAR_W - 56 - sx, COL_MUTED);

    SxRect leave = sx_rect(x0 + SIDEBAR_W - 44, vy + 12, 34, 34);
    icon_button(c, leave, ICON_HANGUP, 22, st == VC_FAILED ? COL_TEXT : COL_HEADER, COL_HOVER, HIT_VC_LEAVE, 0,
                "Disconnect", TIP_ABOVE);

    SxRect share = sx_rect(x0 + 10, vy + 56, SIDEBAR_W - 20, 32);
    bool can_share = st == VC_CONNECTED;
    bool on = ss != SS_OFF;
    bool hov = can_share && hovering(share);
    uint32_t bg = on ? (hov ? COL_RED_HI : COL_RED) : (hov ? COL_SELECTED : COL_HOVER);
    sx_fill_rounded_rect(c, share.x, share.y, share.w, share.h, 6, bg);
    const char *label = ss == SS_OFF ? "Share Screen" : ss == SS_STARTING ? "Cancel" : "Stop Streaming";
    uint32_t fg = !can_share ? COL_FAINT : on ? SX_RGB(0xFF, 0xFF, 0xFF) : COL_HEADER;
    int lw = font_width(FACE_BOLD, 13, label, (int)strlen(label));
    int lx = share.x + (share.w - lw - 26) / 2;
    icon_draw(c, on ? ICON_SCREEN_STOP : ICON_SCREEN, lx, share.y + 7, 18, fg);
    font_draw(c, FACE_BOLD, 13, lx + 26, share.y + 21, label, (int)strlen(label), fg);
    if (can_share)
        add_hit(HIT_VC_STREAM, share, 0);
}

static void draw_user_panel(SxCanvas *c, int x0, int uy)
{
    sx_fill_rect(c, x0, uy, SIDEBAR_W, USER_H, COL_PANEL);
    draw_avatar(c, S.me.id, S.me.avatar, S.me.discriminator, x0 + 10, uy + 10, 32);
    sx_fill_circle(c, x0 + 37.5f, uy + 37.5f, 6.5f, COL_PANEL);
    sx_fill_circle(c, x0 + 37.5f, uy + 37.5f, 4.5f, COL_GREEN);

    int bw = 32;
    int bx = x0 + SIDEBAR_W - 8 - bw * 3;
    const char *me = S.me.global_name && *S.me.global_name ? S.me.global_name : S.me.username ? S.me.username : "";
    font_draw_ellipsis(c, FACE_BOLD, 14, x0 + 52, uy + 23, me, bx - x0 - 56, COL_HEADER);
    font_draw_ellipsis(c, FACE_REGULAR, 12, x0 + 52, uy + 40, S.me.username ? S.me.username : "", bx - x0 - 56, COL_MUTED);

    bool deaf = vc_deafened();
    bool muted = vc_muted() || deaf;
    icon_button(c, sx_rect(bx, uy + 10, bw, bw), muted ? ICON_MIC_OFF : ICON_MIC, 20, muted ? COL_RED : COL_TEXT,
                COL_HOVER, HIT_VC_MUTE, 0, muted ? "Unmute" : "Mute", TIP_ABOVE);
    icon_button(c, sx_rect(bx + bw, uy + 10, bw, bw), deaf ? ICON_HEADPHONES_OFF : ICON_HEADPHONES, 20,
                deaf ? COL_RED : COL_TEXT, COL_HOVER, HIT_VC_DEAF, 0, deaf ? "Undeafen" : "Deafen", TIP_ABOVE);
    icon_button(c, sx_rect(bx + bw * 2, uy + 10, bw, bw), ICON_LOGOUT, 19, COL_TEXT, COL_HOVER, HIT_LOGOUT, 0,
                "Log Out", TIP_ABOVE);
}

static void draw_sidebar(SxCanvas *c)
{
    int x0 = GUILD_W;
    sx_fill_rect(c, x0, 0, SIDEBAR_W, U.h, COL_SIDEBAR);
    Guild *g = U.guild ? store_guild(U.guild) : NULL;

    const char *title = g ? g->name : "Direct Messages";
    font_draw_ellipsis(c, FACE_BOLD, 15, x0 + 16, 30, title, SIDEBAR_W - 32, COL_HEADER);

    int voice_h = vc_state() != VC_OFF ? VOICE_H : 0;
    SxRect list = sx_rect(x0, HEADER_H, SIDEBAR_W, U.h - HEADER_H - USER_H - voice_h);
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
                y += 14;
                char upper[100];
                int k = 0;
                for (const char *p = ch->name ? ch->name : ""; *p && k < 98; p++)
                    upper[k++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
                upper[k] = '\0';
                font_draw_ellipsis(c, FACE_BOLD, 11, x0 + 18, y + 14, upper, SIDEBAR_W - 36, COL_MUTED);
                y += 22;
                continue;
            }
            SxRect r = sx_rect(x0 + 8, y, SIDEBAR_W - 16, 32);
            bool text = store_channel_is_text(ch);
            bool voice = ch->type == CH_VOICE || ch->type == CH_STAGE;
            bool sel = ch->id == U.channel || (voice && ch->id == vc_channel() && vc_state() != VC_OFF);
            bool hov = (text || voice) && hovering(r);
            if (r.y + r.h >= HEADER_H && r.y <= U.h)
            {
                if (sel)
                    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, COL_SELECTED);
                else if (hov)
                    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, COL_HOVER);
                uint32_t col = !(text || voice) ? COL_FAINT : sel ? COL_HEADER : hov ? COL_TEXT : COL_MUTED;
                icon_draw(c, voice ? ICON_SPEAKER : ICON_HASH, r.x + 8, r.y + 6, 20, sel ? COL_TEXT : hov ? COL_MUTED : COL_FAINT);
                font_draw_ellipsis(c, sel ? FACE_BOLD : FACE_REGULAR, 15, r.x + 36, r.y + 21, ch->name ? ch->name : "",
                                   r.w - 44, col);
            }
            if (text)
                add_hit(HIT_CHANNEL, r, ch->id);
            else if (voice)
                add_hit(HIT_VOICE, r, ch->id);
            y += 34;
            if (voice)
                y += draw_voice_members(c, ch, x0 + 38, y - 2, SIDEBAR_W - 54);
        }
    }
    else
    {
        font_draw(c, FACE_BOLD, 11, x0 + 18, y + 18, "DIRECT MESSAGES", 15, COL_MUTED);
        y += 30;
        for (int i = 0; i < S.dm_count; i++)
        {
            Channel *ch = S.dms[i];
            SxRect r = sx_rect(x0 + 8, y, SIDEBAR_W - 16, 42);
            bool sel = ch->id == U.channel;
            bool hov = hovering(r);
            if (r.y + r.h >= HEADER_H && r.y <= U.h)
            {
                if (sel)
                    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, COL_SELECTED);
                else if (hov)
                    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, COL_HOVER);
                if (ch->type == CH_DM && ch->recipient_count > 0)
                {
                    User *u = store_user(ch->recipients[0]);
                    draw_avatar(c, ch->recipients[0], u ? u->avatar : NULL, u ? u->discriminator : NULL, r.x + 8, r.y + 5, 32);
                }
                else
                {
                    sx_fill_circle(c, r.x + 24, r.y + 21, 16, COL_ACCENT);
                    char name[128];
                    store_channel_title(ch, name, sizeof(name));
                    draw_initials(c, name, sx_rect(r.x + 8, r.y + 5, 32, 32), COL_ON_ACCENT);
                }
                bool in_call = vc_channel() == ch->id && vc_state() != VC_OFF;
                int name_w = r.w - 58 - (in_call ? 22 : 0);
                char name[128];
                store_channel_title(ch, name, sizeof(name));
                font_draw_ellipsis(c, sel ? FACE_BOLD : FACE_REGULAR, 15, r.x + 50, r.y + 26, name, name_w,
                                   sel ? COL_HEADER : hov ? COL_TEXT : COL_MUTED);
                if (in_call)
                    icon_draw(c, ICON_SPEAKER, r.x + r.w - 26, r.y + 12, 18, COL_GREEN);
            }
            add_hit(HIT_CHANNEL, r, ch->id);
            y += 44;
        }
        if (S.dm_count == 0)
            text_wrap(c, FACE_REGULAR, 13, x0 + 18, y + 4, SIDEBAR_W - 36, "No direct messages yet.", COL_MUTED, false);
    }
    int content = y + U.side_scroll + 8 - HEADER_H;
    int view = list.h;
    if (U.side_scroll > content - view)
        U.side_scroll = content - view > 0 ? content - view : 0;
    sx_canvas_set_clip(c, old);
    sx_fill_rect(c, x0, HEADER_H - 1, SIDEBAR_W, 1, COL_DIVIDER);
    if (U.side_scroll > 0)
        drop_shadow(c, x0, HEADER_H, SIDEBAR_W);

    int uy = U.h - USER_H;
    if (voice_h)
        draw_voice_panel(c, x0, uy - voice_h);
    draw_user_panel(c, x0, uy);
    sx_fill_rect(c, x0 + SIDEBAR_W - 1, 0, 1, U.h, COL_DIVIDER);
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

static IconId system_icon(const Message *m)
{
    switch (m->type)
    {
        case 3:  return ICON_PHONE;
        case 2:  return ICON_LOGOUT;
        default: return ICON_ARROW;
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
            m->embed_layouts[i] = rich_layout(e->description, inner, 0);
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
        m->layout = rich_layout(m->content, content_w, guild_id);
        m->layout_width = content_w;
        for (int i = 0; i < 4; i++)
        {
            rich_free(m->embed_layouts[i]);
            m->embed_layouts[i] = NULL;
        }
    }
    int h = grouped ? 2 : 16;
    if (m->is_reply)
        h += 22;
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
            h += 62;
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

static int day_of(time_t t)
{
    return (int)(t / 86400);
}

static void format_day(time_t t, char *out, size_t size)
{
    static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December" };
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, size, "%s %d, %d", months[tm.tm_mon % 12], tm.tm_mday, tm.tm_year + 1900);
}

static void draw_day_divider(SxCanvas *c, time_t t, int x, int y, int w)
{
    char day[48];
    format_day(t, day, sizeof(day));
    int len = (int)strlen(day);
    int tw = font_width(FACE_BOLD, 12, day, len);
    int mid = y + DAY_H / 2 + 2;
    int cx = x + w / 2;
    sx_fill_rect(c, x + 16, mid, cx - tw / 2 - 10 - (x + 16), 1, COL_SELECTED);
    sx_fill_rect(c, cx + tw / 2 + 10, mid, x + w - 16 - (cx + tw / 2 + 10), 1, COL_SELECTED);
    font_draw(c, FACE_BOLD, 12, cx - tw / 2, mid + 4, day, len, COL_MUTED);
}

static void draw_embed(SxCanvas *c, Message *m, int i, int x, int y, int width)
{
    Embed *e = &m->embeds[i];
    int h = embed_height(m, i, width);
    int inner = width - 32;
    if (inner > 420)
        inner = 420;
    int box_w = inner + 32;
    uint32_t bar = e->color ? (0xFF000000u | e->color) : COL_SELECTED;
    sx_fill_rounded_rect(c, x, y, box_w, h, 6, COL_EMBED);
    sx_fill_rounded_rect(c, x, y, 4, h, 2, bar);
    int cy = y + 8;
    if (e->author)
    {
        font_draw_ellipsis(c, FACE_BOLD, 13, x + 16, cy + 14, e->author, inner, COL_HEADER);
        cy += 20;
    }
    if (e->title)
    {
        int tw = font_draw_ellipsis(c, FACE_BOLD, 15, x + 16, cy + 16, e->title, inner, e->url ? COL_LINK : COL_HEADER);
        if (e->url)
            link_hit(sx_rect(x + 15, cy, tw + 2, 21), e->url, (int)strlen(e->url), c);
        cy += 22;
    }
    if (e->description && i < 4 && m->embed_layouts[i])
    {
        rich_draw(c, m->embed_layouts[i], x + 16, cy, COL_TEXT);
        rich_links(m->embed_layouts[i], x + 16, cy, link_hit, c);
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
        char name[96];
        url_basename(e->image, name, sizeof(name));
        add_media(c, sx_rect(x + 16, cy + 4, iw, ih), e->image, e->image, name, NULL, e->image_w, e->image_h, 0);
    }
}

static void draw_message(SxCanvas *c, Message *m, int x, int y, int content_w, bool grouped, bool hovered)
{
    int cx = x + GUTTER;
    if (is_system(m))
    {
        int top = y + (grouped ? 4 : 8);
        IconId icon = system_icon(m);
        icon_draw(c, icon, x + 30, top + 1, 18, icon == ICON_LOGOUT ? COL_RED : COL_GREEN);
        int w = font_draw(c, FACE_BOLD, 15, cx, top + 15, m->author_name, (int)strlen(m->author_name), COL_HEADER);
        const char *t = system_text(m);
        w += font_draw(c, FACE_REGULAR, 15, cx + w + 5, top + 15, t, (int)strlen(t), COL_MUTED) + 5;
        char ts[48];
        format_time(m->timestamp, ts, sizeof(ts), true);
        font_draw(c, FACE_REGULAR, UI_SMALL_PX, cx + w + 8, top + 15, ts, (int)strlen(ts), COL_FAINT);
        return;
    }
    if (!grouped)
        y += 16;
    if (m->is_reply)
    {
        uint32_t spine = COL_SELECTED;
        sx_draw_line(c, x + 36, y + 20, x + 36, y + 14, 2, spine);
        icon_arc(c, x + 42, y + 14, 6, 2, 180, 90, spine);
        sx_draw_line(c, x + 42, y + 8, x + 66, y + 8, 2, spine);
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
        y += 22;
    }
    if (!grouped)
    {
        draw_avatar(c, m->author_id, m->author_avatar, m->author_disc, x + 16, y + 2, AVATAR);
        uint32_t name_col = m->author_color ? (0xFF000000u | m->author_color) : COL_HEADER;
        int w = font_draw(c, FACE_BOLD, UI_NAME_PX, cx, y + 16, m->author_name, (int)strlen(m->author_name), name_col);
        if (m->author_bot)
        {
            SxRect tag = sx_rect(cx + w + 6, y + 3, 32, 16);
            sx_fill_rounded_rect(c, tag.x, tag.y, tag.w, tag.h, 4, COL_ACCENT);
            text_center(c, FACE_BOLD, 10, tag, "BOT", COL_ON_ACCENT);
            w += 38;
        }
        char ts[48];
        format_time(m->timestamp, ts, sizeof(ts), true);
        font_draw(c, FACE_REGULAR, UI_SMALL_PX, cx + w + 8, y + 16, ts, (int)strlen(ts), COL_FAINT);
        y += 22;
    }
    else if (hovered)
    {
        char ts[16];
        format_time(m->timestamp, ts, sizeof(ts), false);
        int tw = font_width(FACE_REGULAR, 11, ts, (int)strlen(ts));
        font_draw(c, FACE_REGULAR, 11, x + GUTTER - 10 - tw, y + 15, ts, (int)strlen(ts), COL_FAINT);
    }
    uint32_t color = m->failed ? COL_RED : m->pending ? COL_MUTED : COL_TEXT;
    rich_draw(c, m->layout, cx, y, color);
    rich_links(m->layout, cx, y, link_hit, c);
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
                img_draw(c, img, cx, y + 4, 8);
            else
                sx_fill_rounded_rect(c, cx, y + 4, w, h, 8, COL_EMBED);
            add_media(c, sx_rect(cx, y + 4, w, h), a->url, a->proxy_url ? a->proxy_url : a->url, a->filename,
                      a->content_type, a->width, a->height, a->size);
            y += h + 6;
        }
        else
        {
            int w = content_w < 400 ? content_w : 400;
            sx_fill_rounded_rect(c, cx, y + 4, w, 54, 8, COL_SELECTED);
            sx_fill_rounded_rect(c, cx + 1, y + 5, w - 2, 52, 7, COL_EMBED);
            icon_draw(c, ICON_FILE, cx + 10, y + 13, 36, COL_ACCENT);
            int fw = font_draw_ellipsis(c, FACE_REGULAR, 15, cx + 52, y + 27, a->filename, w - 64, COL_LINK);
            if (a->url)
            {
                SxRect fr = sx_rect(cx + 51, y + 10, fw + 2, 22);
                link_hit(fr, a->url, (int)strlen(a->url), c);
            }
            char size[48];
            if (a->size >= 1024 * 1024)
                snprintf(size, sizeof(size), "%.2f MB", a->size / (1024.0 * 1024.0));
            else
                snprintf(size, sizeof(size), "%.1f KB", a->size / 1024.0);
            font_draw(c, FACE_REGULAR, 12, cx + 52, y + 46, size, (int)strlen(size), COL_FAINT);
            y += 62;
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

static void draw_channel_header(SxCanvas *c, Channel *ch, int x0, int w)
{
    if (!ch)
        return;
    char name[128];
    store_channel_title(ch, name, sizeof(name));
    int hx = x0 + 16;
    icon_draw(c, ch->guild_id ? ICON_HASH : ICON_AT, hx, 12, 24, COL_FAINT);
    hx += 32;
    int right = x0 + w - 16;
    if (!ch->guild_id)
    {
        bool in_call = vc_channel() == ch->id && vc_state() != VC_OFF && vc_state() != VC_FAILED;
        SxRect call = sx_rect(right - 36, 6, 36, 36);
        icon_button(c, call, in_call ? ICON_HANGUP : ICON_PHONE, 22, in_call ? COL_RED : COL_MUTED, COL_INPUT, HIT_CALL,
                    ch->id, in_call ? "Leave Call" : "Start Voice Call", TIP_BELOW);
        if (hovering(call) && !in_call)
            icon_draw(c, ICON_PHONE, call.x + 7, call.y + 7, 22, COL_HEADER);
        right = call.x - 8;
    }
    int nw = font_draw_ellipsis(c, FACE_BOLD, 16, hx, 30, name, right - hx, COL_HEADER);
    if (ch->topic && *ch->topic && right - (hx + nw + 24) > 40)
    {
        sx_fill_rect(c, hx + nw + 12, 15, 1, 20, COL_SELECTED);
        char topic[200];
        int k = 0;
        for (const char *p = ch->topic; *p && k < 190; p++)
            topic[k++] = *p == '\n' ? ' ' : *p;
        topic[k] = '\0';
        font_draw_ellipsis(c, FACE_REGULAR, 13, hx + nw + 24, 30, topic, right - (hx + nw + 24), COL_MUTED);
    }
}

static void draw_messages(SxCanvas *c)
{
    int x0 = GUILD_W + SIDEBAR_W;
    int w = U.w - x0;
    sx_fill_rect(c, x0, 0, w, U.h, COL_MAIN);

    Channel *ch = U.channel ? store_channel(U.channel) : NULL;
    draw_channel_header(c, ch, x0, w);
    sx_fill_rect(c, x0, HEADER_H - 1, w, 1, COL_DIVIDER);

    int input_y = U.h - INPUT_GAP - INPUT_H;
    SxRect area = sx_rect(x0, HEADER_H, w, input_y - 8 - HEADER_H);
    add_hit(HIT_MESSAGES_AREA, area, 0);

    if (!ch)
    {
        const char *msg = S.ready ? (U.guild ? "This server has no text channels you can see." : "Pick a conversation on the left.")
                                  : "Loading...";
        icon_draw(c, U.guild ? ICON_HASH : ICON_HOME, area.x + area.w / 2 - 28, area.y + area.h / 2 - 72, 56, COL_SELECTED);
        text_center(c, FACE_REGULAR, 15, area, msg, COL_MUTED);
        drop_shadow(c, x0, HEADER_H, w);
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
    bool *newday = count ? malloc(sizeof(bool) * (size_t)count) : NULL;
    int total = 0;
    for (int i = 0; heights && grouped && newday && i < count; i++)
    {
        Message *prev = i ? ch->messages[i - 1] : NULL;
        newday[i] = prev ? day_of(prev->timestamp) != day_of(ch->messages[i]->timestamp) : !ch->has_more;
        grouped[i] = !newday[i] && group_with(prev, ch->messages[i]);
        heights[i] = message_height(ch->messages[i], content_w, ch->guild_id, grouped[i]) + (newday[i] ? DAY_H : 0);
        total += heights[i];
    }
    int intro_h = ch->has_more && ch->message_count ? 48 : 150;
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
    for (int i = count - 1; heights && grouped && newday && i >= 0; i--)
    {
        int top = yb - heights[i];
        if (top < area.y + area.h && yb > area.y)
        {
            Message *m = ch->messages[i];
            int mt = top;
            if (newday[i])
            {
                draw_day_divider(c, m->timestamp, x0, top, w);
                mt += DAY_H;
            }
            bool hov = hovering(sx_rect(x0, mt, w, yb - mt)) && sx_rect_contains(area, U.hover_x, U.hover_y);
            if (hov)
                sx_fill_rect(c, x0, mt + (grouped[i] ? 0 : 8), w, yb - mt - (grouped[i] ? 0 : 8), COL_MSG_HOVER);
            draw_message(c, m, x0, mt, content_w, grouped[i], hov);
        }
        yb = top;
    }
    int intro_top = yb - intro_h;
    if (intro_top + intro_h > area.y)
    {
        if (ch->has_more && ch->message_count)
        {
            if (ch->loading)
                icon_spinner(c, x0 + w / 2, intro_top + intro_h / 2, 9, 3, uptime_ms() / 900.0f, COL_MUTED);
        }
        else if (ch->loaded)
        {
            char name[128];
            store_channel_title(ch, name, sizeof(name));
            int ix = x0 + 16;
            sx_fill_circle(c, ix + 34, intro_top + 34, 34, COL_SELECTED);
            icon_draw(c, ch->guild_id ? ICON_HASH : ICON_AT, ix + 12, intro_top + 12, 44, COL_HEADER);
            char line[200];
            snprintf(line, sizeof(line), ch->guild_id ? "Welcome to #%s!" : "%s", name);
            font_draw_ellipsis(c, FACE_BOLD, 26, ix, intro_top + 104, line, w - 32, COL_HEADER);
            const char *sub = ch->guild_id ? "This is the start of the channel." : "This is the beginning of your direct message history.";
            font_draw_ellipsis(c, FACE_REGULAR, 14, ix, intro_top + 130, sub, w - 32, COL_MUTED);
        }
    }
    free(heights);
    free(grouped);
    free(newday);

    if (!ch->loaded)
    {
        if (ch->load_failed)
        {
            text_center(c, FACE_REGULAR, 15, sx_rect(area.x, area.y + area.h / 2 - 34, area.w, 24), ch->error, COL_MUTED);
            SxRect btn = sx_rect(area.x + area.w / 2 - 50, area.y + area.h / 2, 100, 34);
            sx_fill_rounded_rect(c, btn.x, btn.y, btn.w, btn.h, 6, hovering(btn) ? COL_ACCENT_HI : COL_ACCENT);
            text_center(c, FACE_BOLD, 14, btn, "Retry", COL_ON_ACCENT);
            add_hit(HIT_RETRY_LOAD, btn, ch->id);
        }
        else
        {
            icon_spinner(c, area.x + area.w / 2, area.y + area.h / 2 - 14, 14, 4, uptime_ms() / 900.0f, COL_ACCENT);
            text_center(c, FACE_REGULAR, 14, sx_rect(area.x, area.y + area.h / 2 + 10, area.w, 24), "Loading messages...", COL_MUTED);
        }
    }

    if (total > area.h)
    {
        int track = area.h - 8;
        int thumb = track * area.h / total;
        if (thumb < 32)
            thumb = 32;
        int pos = (int)((long)(track - thumb) * (max_scroll - U.msg_scroll) / (max_scroll ? max_scroll : 1));
        sx_fill_rounded_rect(c, U.w - 12, area.y + 4, 8, track, 4, COL_EMBED);
        sx_fill_rounded_rect(c, U.w - 12, area.y + 4 + pos, 8, thumb, 4, COL_GUILDBAR);
    }
    sx_canvas_set_clip(c, old);
    drop_shadow(c, x0, HEADER_H, w);

    if (ch->loaded && ch->has_more && !ch->loading && U.msg_scroll + area.h > total - 600)
        load_messages(ch, true);

    SxRect in = sx_rect(x0 + 16, input_y, w - 32, INPUT_H);
    sx_fill_rounded_rect(c, in.x, in.y, in.w, in.h, 10, COL_INPUT);
    char placeholder[160];
    char name[128];
    store_channel_title(ch, name, sizeof(name));
    snprintf(placeholder, sizeof(placeholder), "Message %s%s", ch->guild_id ? "#" : "@", name);
    draw_field(c, &U.input, sx_rect(in.x + 4, in.y, in.w - 8, in.h), U.focused, false, placeholder, 15);
    add_hit(HIT_INPUT, in, 0);

    if (U.status[0] && uptime_ms() - U.status_at < 5000)
    {
        uint32_t sc = U.status_ok ? COL_GREEN : COL_RED;
        icon_draw(c, U.status_ok ? ICON_CHECK : ICON_CLOSE, in.x + 2, U.h - 19, 13, sc);
        font_draw_ellipsis(c, FACE_REGULAR, 12, in.x + 20, U.h - 8, U.status, in.w - 20, sc);
    }
}

static void draw_qr_panel(SxCanvas *c, SxRect r)
{
    RaState st = ra_state();
    SxRect box = sx_rect(r.x + (r.w - 176) / 2, r.y, 176, 176);
    sx_fill_rounded_rect(c, box.x, box.y, box.w, box.h, 10, SX_RGB(0xFF, 0xFF, 0xFF));

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
        SxRect inner = sx_rect(box.x + 8, box.y + 8, box.w - 16, box.h - 16);
        bool hov = hovering(box);
        sx_fill_rounded_rect(c, inner.x, inner.y, inner.w, inner.h, 8, hov ? SX_RGB(0xE6, 0xE7, 0xEA) : SX_RGB(0xF2, 0xF3, 0xF5));
        text_center(c, FACE_BOLD, 14, sx_rect(box.x, box.y + 60, box.w, 24), "Click to refresh", SX_RGB(0x31, 0x33, 0x38));
        add_hit(HIT_QR_RETRY, box, 0);
    }
    else
    {
        icon_spinner(c, box.x + box.w / 2, box.y + box.h / 2, 18, 4, uptime_ms() / 900.0f, SX_RGB(0x5C, 0x5E, 0x66));
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
    sx_fill_gradient(c, 0, 0, U.w, U.h, sx_mix(COL_GUILDBAR, COL_ACCENT, 70), COL_GUILDBAR);
    sx_fill_circle(c, U.w * 0.12f, U.h * 0.88f, U.h * 0.35f, (COL_ACCENT & 0xFFFFFFu) | 0x10000000u);
    sx_fill_circle(c, U.w * 0.9f, U.h * 0.1f, U.h * 0.28f, (COL_ACCENT & 0xFFFFFFu) | 0x0C000000u);
    int cw = 780, ch = 440;
    if (cw > U.w - 32)
        cw = U.w - 32;
    int cx = (U.w - cw) / 2, cy = (U.h - ch) / 2;
    sx_fill_rounded_rect(c, cx - 2, cy + 4, cw + 4, ch + 6, 14, COL_SHADOW);
    sx_fill_rounded_rect(c, cx, cy, cw, ch, 12, COL_MAIN);

    int lw = cw - 64 - 296;
    int lx = cx + 32;
    text_center(c, FACE_BOLD, 24, sx_rect(lx, cy + 36, lw, 32), "Welcome back!", COL_HEADER);
    text_center(c, FACE_REGULAR, 15, sx_rect(lx, cy + 70, lw, 22), "We're so excited to see you again!", COL_MUTED);

    int fy = cy + 128;
    font_draw(c, FACE_BOLD, 12, lx, fy, "TOKEN", 5, COL_MUTED);
    font_draw(c, FACE_BOLD, 12, lx + font_width(FACE_BOLD, 12, "TOKEN", 5) + 4, fy, "*", 1, COL_RED);
    SxRect field = sx_rect(lx, fy + 10, lw, 42);
    bool focus = U.token_focus && U.focused;
    if (focus)
        sx_fill_rounded_rect(c, field.x - 2, field.y - 2, field.w + 4, field.h + 4, 8, COL_ACCENT);
    sx_fill_rounded_rect(c, field.x, field.y, field.w, field.h, 6, COL_GUILDBAR);
    draw_field(c, &U.token, field, focus, true, "Paste your token", 15);
    add_hit(HIT_TOKEN, field, 0);

    SxRect btn = sx_rect(lx, fy + 68, lw, 44);
    sx_fill_rounded_rect(c, btn.x, btn.y, btn.w, btn.h, 6, hovering(btn) ? COL_ACCENT_HI : COL_ACCENT);
    text_center(c, FACE_BOLD, 15, btn, "Log In", COL_ON_ACCENT);
    add_hit(HIT_LOGIN, btn, 0);

    int ny = fy + 130;
    if (U.login_error[0])
    {
        icon_draw(c, ICON_CLOSE, lx, ny + 1, 14, COL_RED);
        ny += text_wrap(c, FACE_REGULAR, 13, lx + 20, ny, lw - 20, U.login_error, COL_RED, false) + 8;
    }
    ny += text_wrap(c, FACE_REGULAR, 12, lx, ny, lw,
                    "Bot accounts: type \"Bot \" before the token. The token is saved in /config/discord/token, "
                    "so you can also place it there.",
                    COL_FAINT, false) + 6;
    text_wrap(c, FACE_REGULAR, 12, lx, ny, lw,
              "Third-party clients are against Discord's Terms of Service. Use at your own risk.", COL_FAINT, false);

    sx_fill_rect(c, cx + cw - 280 - 16, cy + 36, 1, ch - 72, COL_SELECTED);
    draw_qr_panel(c, sx_rect(cx + cw - 280, cy + 44, 248, ch - 88));
}

static void draw_connecting(SxCanvas *c)
{
    sx_fill_rect(c, 0, 0, U.w, U.h, COL_MAIN);
    float cx = U.w / 2.0f, cy = U.h / 2.0f - 46;
    sx_fill_circle(c, cx, cy, 30, COL_ACCENT);
    icon_draw(c, ICON_HOME, (int)cx - 16, (int)cy - 16, 32, COL_ON_ACCENT);
    icon_spinner(c, (int)cx, (int)cy, 40, 4, uptime_ms() / 1000.0f, COL_ACCENT);
    text_center(c, FACE_BOLD, 18, sx_rect(0, U.h / 2 + 8, U.w, 30), "Connecting to Discord", COL_HEADER);
    text_center(c, FACE_REGULAR, 13, sx_rect(0, U.h / 2 + 38, U.w, 20), "Loading your servers and messages.", COL_MUTED);
}

static void format_size(long size, char *out, size_t n)
{
    if (size >= 1024 * 1024)
        snprintf(out, n, "%.2f MB", size / (1024.0 * 1024.0));
    else if (size > 0)
        snprintf(out, n, "%.1f KB", size / 1024.0);
    else
        out[0] = '\0';
}

static void draw_viewer(SxCanvas *c)
{
    Viewer *v = &U.view;
    sx_fill_rect(c, 0, 0, U.w, U.h, (sx_darken(COL_GUILDBAR, 120) & 0xFFFFFFu) | 0xE0000000u);
    add_hit(HIT_VIEW_BG, sx_rect(0, 0, U.w, U.h), 0);

    int max_w = U.w - 96, max_h = U.h - 172;
    if (max_w < 64)
        max_w = 64;
    if (max_h < 64)
        max_h = 64;
    int w, h;
    fit_box(v->w, v->h, max_w, max_h, &w, &h);
    int x = (U.w - w) / 2, y = 80 + (max_h - h) / 2;

    char url[1400];
    media_url(v->proxy, w, h, v->ctype, url, sizeof(url));
    SxImage *img;
    ImgState st = img_request(url, w, h, &img);
    if (st == IMG_READY && img)
        img_draw(c, img, x, y, 8);
    else
    {
        sx_fill_rounded_rect(c, x, y, w, h, 8, (COL_MAIN & 0xFFFFFFu) | 0x80000000u);
        if (st == IMG_FAILED)
            text_center(c, FACE_REGULAR, 14, sx_rect(x, y, w, h), "Couldn't load this image.", COL_MUTED);
        else
            icon_spinner(c, x + w / 2, y + h / 2, 16, 4, uptime_ms() / 900.0f, COL_HEADER);
    }
    add_hit(HIT_VIEW_IMG, sx_rect(x, y, w, h), 0);

    char info[160], size[32];
    format_size(v->size, size, sizeof(size));
    if (v->w > 0 && v->h > 0)
        snprintf(info, sizeof(info), "%d \xc3\x97 %d%s%s", v->w, v->h, size[0] ? "  \xc2\xb7  " : "", size);
    else
        snprintf(info, sizeof(info), "%s", size);
    int ty = y + h + 28;
    text_center(c, FACE_BOLD, 14, sx_rect(x - 200, ty - 16, w + 400, 20), v->name, COL_HEADER);
    if (info[0])
        text_center(c, FACE_REGULAR, 12, sx_rect(x - 200, ty + 4, w + 400, 18), info, COL_MUTED);

    int bw = 40, gap = 4, n = 3;
    int tw = n * bw + (n - 1) * gap + 12;
    SxRect bar = sx_rect(U.w - tw - 16, 14, tw, bw + 12);
    sx_fill_rounded_rect(c, bar.x, bar.y + 2, bar.w, bar.h, 12, COL_SHADOW);
    sx_fill_rounded_rect(c, bar.x, bar.y, bar.w, bar.h, 12, COL_PANEL);
    int bx = bar.x + 6, by = bar.y + 6;
    icon_button(c, sx_rect(bx, by, bw, bw), ICON_DOWNLOAD, 22, COL_HEADER, COL_HOVER, HIT_VIEW_SAVE, 0,
                "Download", TIP_BELOW);
    icon_button(c, sx_rect(bx + bw + gap, by, bw, bw), ICON_EXTERNAL, 21, COL_HEADER, COL_HOVER, HIT_VIEW_OPEN, 0,
                "Open in NetSurf", TIP_BELOW);
    icon_button(c, sx_rect(bx + 2 * (bw + gap), by, bw, bw), ICON_CLOSE, 22, COL_HEADER, COL_HOVER, HIT_VIEW_CLOSE, 0,
                "Close (Esc)", TIP_BELOW);

    if (U.status[0] && uptime_ms() - U.status_at < 5000)
    {
        int sw = font_width(FACE_BOLD, 13, U.status, (int)strlen(U.status)) + 52;
        if (sw > U.w - 32)
            sw = U.w - 32;
        SxRect toast = sx_rect((U.w - sw) / 2, 22, sw, 36);
        sx_fill_rounded_rect(c, toast.x, toast.y, toast.w, toast.h, 18, U.status_ok ? COL_GREEN : COL_RED);
        icon_draw(c, U.status_ok ? ICON_CHECK : ICON_CLOSE, toast.x + 12, toast.y + 9, 18, SX_RGB(0xFF, 0xFF, 0xFF));
        font_draw_ellipsis(c, FACE_BOLD, 13, toast.x + 38, toast.y + 23, U.status, toast.w - 50, SX_RGB(0xFF, 0xFF, 0xFF));
    }
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(U.win);
    U.w = c->width;
    U.h = c->height;
    U.hit_count = 0;
    U.tip[0] = '\0';
    U.pool_len = 0;
    U.media_count = 0;
    pal_refresh();
    bool viewing = U.view.open && U.screen == SCREEN_MAIN;
    int hx = U.hover_x, hy = U.hover_y;
    if (viewing)
        U.hover_x = U.hover_y = -1;
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
    sx_canvas_reset_clip(c);
    if (viewing)
    {
        U.hover_x = hx;
        U.hover_y = hy;
        U.tip[0] = '\0';
        draw_viewer(c);
    }
    draw_tooltip(c);
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
        case HIT_VOICE:
            vc_join(store_channel(h->id));
            break;
        case HIT_CALL:
        {
            Channel *ch = store_channel(h->id);
            if (vc_channel() == h->id && vc_state() != VC_OFF && vc_state() != VC_FAILED)
                vc_leave();
            else if (ch)
            {
                vc_join(ch);
                char path[96];
                snprintf(path, sizeof(path), "/channels/%llu/call/ring", (unsigned long long)ch->id);
                api_request("POST", path, "{\"recipients\":null}", NULL, NULL);
            }
            break;
        }
        case HIT_VC_MUTE:
            vc_set_mute(!(vc_muted() || vc_deafened()));
            break;
        case HIT_VC_DEAF:
            vc_set_deaf(!vc_deafened());
            break;
        case HIT_VC_STREAM:
            if (vc_stream_state() == SS_OFF)
                vc_stream_start();
            else
                vc_stream_stop();
            break;
        case HIT_WATCH:
            vc_watch(h->id);
            break;
        case HIT_VC_LEAVE:
            vc_leave();
            break;
        case HIT_LINK:
            open_url(pool_get((int)h->id));
            break;
        case HIT_IMAGE:
            if (h->id < (Snowflake)U.media_count)
                viewer_open(&U.media[h->id]);
            break;
        case HIT_VIEW_BG:
        case HIT_VIEW_CLOSE:
            viewer_close();
            break;
        case HIT_VIEW_SAVE:
            download_view();
            break;
        case HIT_VIEW_OPEN:
            if (U.view.open)
                open_url(U.view.url);
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
    if (U.view.open)
        return;
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
    if (U.view.open)
    {
        if (ev->key == SX_KEY_ESCAPE)
            viewer_close();
        else if ((ev->mods & SX_MOD_CTRL) && (ev->key == 's' || ev->ch == 's' || ev->ch == 19))
            download_view();
        return;
    }
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
        n += vc_pollfds(fds + n, 48 - n);

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
        if (U.screen == SCREEN_CONNECTING && timeout > 33)
            timeout = 33;
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
        vc_step();
        update_state();
        if (vc_take_changed())
            U.dirty = true;

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

    vc_leave();
    gw_stop();
    ra_stop();
    sx_window_destroy(U.win);
    sx_disconnect();
    return 0;
}
