#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <poll.h>
#include <time.h>

#include <strawx.h>
#include <ui.h>

typedef uint64_t Snowflake;

typedef struct
{
    char  *data;
    size_t len;
    size_t cap;
}
Buf;

void buf_append(Buf *b, const void *data, size_t len);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, char c);
void buf_printf(Buf *b, const char *fmt, ...);
void buf_json_string(Buf *b, const char *s);
void buf_consume(Buf *b, size_t n);
void buf_free(Buf *b);

char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);
size_t base64_encode(const uint8_t *in, size_t len, char *out, bool url);
size_t base64_decode(const char *in, size_t len, uint8_t *out);
int    utf8_decode(const char *s, int len, uint32_t *cp);
void   random_bytes(void *out, size_t len);
Snowflake parse_snowflake(const char *s);
time_t parse_iso8601(const char *s);

typedef enum
{
    JSON_NULL,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
}
JsonType;

typedef struct Json Json;

struct Json
{
    JsonType    type;
    int         count;
    const char *key;
    const char *str;
    double      num;
    Json       *child;
    Json       *next;
};

typedef struct JsonDoc JsonDoc;

JsonDoc    *json_parse(const char *text, size_t len);
Json       *json_root(JsonDoc *doc);
void        json_free(JsonDoc *doc);
Json       *json_get(const Json *obj, const char *key);
const char *json_str(const Json *obj, const char *key);
int64_t     json_int(const Json *obj, const char *key, int64_t def);
bool        json_bool(const Json *obj, const char *key);
Snowflake   json_id(const Json *obj, const char *key);
Snowflake   json_as_id(const Json *v);

typedef struct Conn Conn;

Conn       *conn_open(const char *host, uint16_t port, bool tls);
int         conn_step(Conn *c);
bool        conn_open_ready(Conn *c);
bool        conn_failed(Conn *c);
void        conn_write(Conn *c, const void *data, size_t len);
int         conn_read(Conn *c, void *buf, size_t len);
const char *conn_error(Conn *c);
void        conn_close(Conn *c);
int         conn_fd(Conn *c);
short       conn_poll_events(Conn *c);
bool        conn_has_buffered(Conn *c);

typedef void (*HttpDone)(int status, const uint8_t *body, size_t len, void *user);

bool http_request(const char *method, const char *url, const char *headers,
                  const char *body, size_t body_len, HttpDone done, void *user);
void http_pump(void);
int  http_pollfds(struct pollfd *fds, int max);
bool http_busy(void);
void http_cancel(void *user);

typedef enum
{
    WS_CONNECTING,
    WS_OPEN,
    WS_CLOSED,
}
WsState;

typedef struct Ws Ws;

Ws         *ws_open(const char *url, const char *origin);
WsState     ws_step(Ws *ws);
bool        ws_recv(Ws *ws, char **msg, size_t *len);
void        ws_send(Ws *ws, const char *text, size_t len);
int         ws_close_code(Ws *ws);
const char *ws_error(Ws *ws);
int         ws_pollfd(Ws *ws, struct pollfd *pfd);
bool        ws_has_buffered(Ws *ws);
void        ws_free(Ws *ws);

typedef struct
{
    int      size;
    uint8_t *modules;
}
QrCode;

bool qr_encode(const char *text, QrCode *out);
bool qr_module(const QrCode *qr, int x, int y);
void qr_free(QrCode *qr);

typedef enum
{
    FACE_REGULAR,
    FACE_BOLD,
    FACE_ITALIC,
    FACE_MONO,
    FACE_COUNT,
}
FaceId;

bool font_init(void);
int  font_ascent(int face, int px);
int  font_line_height(int face, int px);
int  font_width(int face, int px, const char *s, int len);
int  font_draw(SxCanvas *c, int face, int px, int x, int y, const char *s, int len, uint32_t color);
int  font_fit(int face, int px, const char *s, int len, int max_width);
int  font_draw_ellipsis(SxCanvas *c, int face, int px, int x, int y, const char *s, int max_width, uint32_t color);

typedef enum
{
    IMG_NONE,
    IMG_LOADING,
    IMG_READY,
    IMG_FAILED,
}
ImgState;

ImgState  img_request(const char *url, int w, int h, SxImage **out);
void      img_draw(SxCanvas *c, const SxImage *img, int x, int y, int radius);
void      img_draw_circle(SxCanvas *c, const SxImage *img, int x, int y, int d);
bool      img_take_changed(void);

#define CH_TEXT          0
#define CH_DM            1
#define CH_VOICE         2
#define CH_GROUP_DM      3
#define CH_CATEGORY      4
#define CH_ANNOUNCEMENT  5
#define CH_STAGE         13
#define CH_FORUM         15

typedef struct
{
    char *url;
    char *proxy_url;
    char *filename;
    char *content_type;
    int   width, height;
    long  size;
}
Attachment;

typedef struct
{
    char    *title;
    char    *description;
    char    *author;
    char    *url;
    char    *image;
    int      image_w, image_h;
    uint32_t color;
}
Embed;

typedef struct Message Message;

struct Message
{
    Snowflake   id;
    Snowflake   channel_id;
    Snowflake   author_id;
    char       *author_name;
    char       *author_avatar;
    char       *author_disc;
    uint32_t    author_color;
    bool        author_bot;
    char       *content;
    time_t      timestamp;
    bool        edited;
    int         type;
    char       *nonce;
    bool        pending;
    bool        failed;

    Attachment *attachments;
    int         attachment_count;
    Embed      *embeds;
    int         embed_count;

    char       *reply_author;
    char       *reply_content;
    bool        is_reply;

    void       *layout;
    int         layout_width;
    void       *embed_layouts[4];
    int         height;
    bool        grouped;
};

typedef struct
{
    Snowflake id;
    int       type;
    uint64_t  allow;
    uint64_t  deny;
}
Overwrite;

typedef struct Channel Channel;

struct Channel
{
    Snowflake  id;
    Snowflake  guild_id;
    Snowflake  parent_id;
    int        type;
    int        position;
    char      *name;
    char      *topic;
    Snowflake  last_message_id;

    Snowflake *recipients;
    int        recipient_count;
    char      *icon;

    Overwrite *overwrites;
    int        overwrite_count;

    Message  **messages;
    int        message_count;
    int        message_cap;
    bool       loaded;
    bool       loading;
    bool       has_more;
    bool       load_failed;
    char       error[96];
};

typedef struct
{
    Snowflake id;
    uint64_t  permissions;
    int       position;
    uint32_t  color;
    char     *name;
}
Role;

typedef struct
{
    Snowflake  id;
    char      *name;
    char      *icon;
    Snowflake  owner_id;

    Channel  **channels;
    int        channel_count;

    Role      *roles;
    int        role_count;

    Snowflake *my_roles;
    int        my_role_count;
    bool       have_member;
}
Guild;

typedef struct
{
    Snowflake id;
    char     *username;
    char     *global_name;
    char     *avatar;
    char     *discriminator;
}
User;

typedef struct
{
    User      me;
    bool      ready;

    Guild   **guilds;
    int       guild_count;

    Channel **dms;
    int       dm_count;

    int       version;
}
Store;

extern Store S;

void     store_reset(void);
void     store_on_ready(Json *d);
void     store_on_guild_create(Json *g);
void     store_on_guild_delete(Json *g);
void     store_on_channel(Json *c, bool deleted);
void     store_on_message_create(Json *m);
void     store_on_message_update(Json *m);
void     store_on_message_delete(Json *m);
void     store_load_messages(Channel *ch, Json *arr, bool older);
Channel *store_channel(Snowflake id);
Guild   *store_guild(Snowflake id);
User    *store_user(Snowflake id);
bool     store_channel_visible(Guild *g, Channel *ch);
bool     store_channel_is_text(const Channel *ch);
void     store_channel_title(const Channel *ch, char *out, size_t size);
Message *store_add_pending(Channel *ch, const char *content, const char *nonce);
void     store_mark_failed(Channel *ch, const char *nonce);
void     message_free(Message *m);
const char *store_role_name(Snowflake guild_id, Snowflake role_id);
const char *store_user_name(Snowflake id);
void     user_avatar_url(Snowflake id, const char *avatar, const char *disc, char *out, size_t size);

typedef enum
{
    GW_OFF,
    GW_CONNECTING,
    GW_READY,
    GW_FAILED,
}
GwState;

void        gw_start(const char *token);
void        gw_stop(void);
void        gw_step(void);
GwState     gw_state(void);
const char *gw_error(void);
int         gw_pollfds(struct pollfd *fds, int max);
bool        gw_has_buffered(void);
uint64_t    gw_next_deadline(void);

typedef enum
{
    RA_OFF,
    RA_STARTING,
    RA_WAITING,
    RA_SCANNED,
    RA_DONE,
    RA_FAILED,
}
RaState;

void        ra_start(void);
void        ra_stop(void);
void        ra_step(void);
RaState     ra_state(void);
const char *ra_fingerprint(void);
const char *ra_user(void);
const char *ra_user_avatar(void);
const char *ra_token(void);
const char *ra_error(void);
int         ra_pollfds(struct pollfd *fds, int max);
bool        ra_has_buffered(void);
uint64_t    ra_next_deadline(void);

extern char api_base[256];
extern char gateway_url[256];
extern char cdn_base[256];
extern char auth_token[256];

const char *api_headers(void);
bool api_request(const char *method, const char *path, const char *json, HttpDone done, void *user);

typedef struct RichLayout RichLayout;

RichLayout *rich_layout(const char *text, int width, Snowflake guild_id, uint32_t base_color);
int         rich_height(const RichLayout *l);
void        rich_draw(SxCanvas *c, const RichLayout *l, int x, int y);
void        rich_free(RichLayout *l);

#define UI_TEXT_PX   15
#define UI_SMALL_PX  12
#define UI_NAME_PX   15

#define COL_GUILDBAR    SX_RGB(0x1E, 0x1F, 0x22)
#define COL_SIDEBAR     SX_RGB(0x2B, 0x2D, 0x31)
#define COL_MAIN        SX_RGB(0x31, 0x33, 0x38)
#define COL_INPUT       SX_RGB(0x38, 0x3A, 0x40)
#define COL_PANEL       SX_RGB(0x23, 0x24, 0x28)
#define COL_HOVER       SX_RGB(0x35, 0x37, 0x3C)
#define COL_SELECTED    SX_RGB(0x40, 0x42, 0x49)
#define COL_TEXT        SX_RGB(0xDB, 0xDE, 0xE1)
#define COL_HEADER      SX_RGB(0xF2, 0xF3, 0xF5)
#define COL_MUTED       SX_RGB(0x94, 0x9B, 0xA4)
#define COL_FAINT       SX_RGB(0x6D, 0x6F, 0x78)
#define COL_ACCENT      SX_RGB(0x58, 0x65, 0xF2)
#define COL_ACCENT_HI   SX_RGB(0x47, 0x52, 0xC4)
#define COL_LINK        SX_RGB(0x00, 0xA8, 0xFC)
#define COL_MENTION_BG  SX_RGB(0x3C, 0x42, 0x70)
#define COL_MENTION     SX_RGB(0xC9, 0xCD, 0xFB)
#define COL_CODE_BG     SX_RGB(0x2B, 0x2D, 0x31)
#define COL_DIVIDER     SX_RGB(0x1F, 0x20, 0x23)
#define COL_RED         SX_RGB(0xF2, 0x3F, 0x43)
#define COL_GREEN       SX_RGB(0x23, 0xA5, 0x59)
