#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discord.h"

#define ST_BOLD    0x01
#define ST_ITALIC  0x02
#define ST_CODE    0x04
#define ST_BLOCK   0x08
#define ST_LINK    0x10
#define ST_MENTION 0x20
#define ST_STRIKE  0x40
#define ST_QUOTE   0x80
#define ST_UNDER   0x100
#define ST_SPOILER 0x200

typedef struct
{
    int      start;
    int      len;
    uint16_t style;
}
Span;

typedef struct
{
    int      start;
    int      len;
    int      x;
    int      line;
    int      width;
    uint16_t style;
}
Piece;

struct RichLayout
{
    char     *text;
    Piece    *pieces;
    int       count;
    int       lines;
    int       line_h;
    int       width;
    uint32_t  color;
    bool     *block_lines;
    bool     *quote_lines;
};

typedef struct
{
    Buf      text;
    Span    *spans;
    int      count;
    int      cap;
    Snowflake guild;
}
Builder;

static void emit(Builder *b, const char *s, int len, uint16_t style)
{
    if (len <= 0)
        return;
    if (b->count && b->spans[b->count - 1].style == style &&
        b->spans[b->count - 1].start + b->spans[b->count - 1].len == (int)b->text.len)
    {
        buf_append(&b->text, s, (size_t)len);
        b->spans[b->count - 1].len += len;
        return;
    }
    if (b->count == b->cap)
    {
        int cap = b->cap ? b->cap * 2 : 32;
        Span *n = realloc(b->spans, sizeof(Span) * (size_t)cap);
        if (!n)
            return;
        b->spans = n;
        b->cap = cap;
    }
    b->spans[b->count].start = (int)b->text.len;
    b->spans[b->count].len = len;
    b->spans[b->count].style = style;
    b->count++;
    buf_append(&b->text, s, (size_t)len);
}

static bool has_closing(const char *p, const char *marker)
{
    const char *q = strstr(p, marker);
    return q != NULL;
}

static int parse_angle(Builder *b, const char *p, uint16_t style)
{
    const char *end = strchr(p, '>');
    if (!end || end - p > 120)
        return 0;
    int len = (int)(end - p) + 1;
    char inner[128];
    memcpy(inner, p + 1, (size_t)len - 2);
    inner[len - 2] = '\0';
    char out[160];

    if (inner[0] == '@' && inner[1] == '&')
    {
        const char *name = store_role_name(b->guild, parse_snowflake(inner + 2));
        snprintf(out, sizeof(out), "@%s", name ? name : "role");
        emit(b, out, (int)strlen(out), style | ST_MENTION);
        return len;
    }
    if (inner[0] == '@')
    {
        const char *id = inner + 1 + (inner[1] == '!');
        if (*id < '0' || *id > '9')
            return 0;
        const char *name = store_user_name(parse_snowflake(id));
        snprintf(out, sizeof(out), "@%s", name ? name : "unknown-user");
        emit(b, out, (int)strlen(out), style | ST_MENTION);
        return len;
    }
    if (inner[0] == '#')
    {
        Channel *ch = store_channel(parse_snowflake(inner + 1));
        snprintf(out, sizeof(out), "#%s", ch && ch->name ? ch->name : "unknown");
        emit(b, out, (int)strlen(out), style | ST_MENTION);
        return len;
    }
    if (inner[0] == ':' || (inner[0] == 'a' && inner[1] == ':'))
    {
        const char *name = inner + (inner[0] == 'a' ? 2 : 1);
        const char *colon = strchr(name, ':');
        if (!colon)
            return 0;
        snprintf(out, sizeof(out), ":%.*s:", (int)(colon - name), name);
        emit(b, out, (int)strlen(out), style);
        return len;
    }
    if (inner[0] == 't' && inner[1] == ':')
    {
        time_t t = (time_t)strtoll(inner + 2, NULL, 10);
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(out, sizeof(out), "%Y-%m-%d %H:%M UTC", &tm);
        emit(b, out, (int)strlen(out), style | ST_CODE);
        return len;
    }
    if (strncmp(inner, "http://", 7) == 0 || strncmp(inner, "https://", 8) == 0)
    {
        emit(b, inner, (int)strlen(inner), style | ST_LINK);
        return len;
    }
    return 0;
}

static void parse(Builder *b, const char *s)
{
    uint16_t style = 0;
    bool line_start = true;
    const char *p = s;
    while (*p)
    {
        if (line_start && p[0] == '>' && p[1] == ' ' && !(style & ST_BLOCK))
        {
            style |= ST_QUOTE;
            p += 2;
            line_start = false;
            continue;
        }
        line_start = false;
        if (*p == '\n')
        {
            emit(b, "\n", 1, style & ~ST_QUOTE);
            if (!(style & ST_BLOCK))
                style &= ~ST_QUOTE;
            line_start = true;
            p++;
            continue;
        }
        if (p[0] == '`' && p[1] == '`' && p[2] == '`')
        {
            if (style & ST_BLOCK)
            {
                style &= ~ST_BLOCK;
                p += 3;
                if (*p == '\n')
                    p++;
                emit(b, "\n", 1, 0);
                continue;
            }
            if (has_closing(p + 3, "```"))
            {
                if (b->text.len && b->text.data[b->text.len - 1] != '\n')
                    emit(b, "\n", 1, style);
                style |= ST_BLOCK;
                p += 3;
                const char *q = p;
                while (*q && *q != '\n' && *q != ' ' && *q != '`')
                    q++;
                if (*q == '\n')
                    p = q + 1;
                else if (*p == '\n')
                    p++;
                continue;
            }
        }
        if (style & ST_BLOCK)
        {
            const char *q = p;
            while (*q && *q != '\n' && !(q[0] == '`' && q[1] == '`' && q[2] == '`'))
                q++;
            emit(b, p, (int)(q - p), ST_BLOCK);
            p = q;
            continue;
        }
        if (*p == '\\' && p[1] && strchr("*_`~|\\<>#", p[1]))
        {
            emit(b, p + 1, 1, style);
            p += 2;
            continue;
        }
        if (*p == '`')
        {
            const char *q = strchr(p + 1, '`');
            const char *nl = strchr(p + 1, '\n');
            if (q && (!nl || q < nl) && q > p + 1)
            {
                emit(b, p + 1, (int)(q - p - 1), style | ST_CODE);
                p = q + 1;
                continue;
            }
        }
        if (p[0] == '*' && p[1] == '*' && ((style & ST_BOLD) || has_closing(p + 2, "**")))
        {
            style ^= ST_BOLD;
            p += 2;
            continue;
        }
        if (p[0] == '_' && p[1] == '_' && ((style & ST_UNDER) || has_closing(p + 2, "__")))
        {
            style ^= ST_UNDER;
            p += 2;
            continue;
        }
        if (p[0] == '~' && p[1] == '~' && ((style & ST_STRIKE) || has_closing(p + 2, "~~")))
        {
            style ^= ST_STRIKE;
            p += 2;
            continue;
        }
        if (p[0] == '|' && p[1] == '|' && ((style & ST_SPOILER) || has_closing(p + 2, "||")))
        {
            style ^= ST_SPOILER;
            p += 2;
            continue;
        }
        if (p[0] == '*' && p[1] != ' ' && ((style & ST_ITALIC) || has_closing(p + 1, "*")))
        {
            style ^= ST_ITALIC;
            p++;
            continue;
        }
        if (*p == '<')
        {
            int n = parse_angle(b, p, style);
            if (n > 0)
            {
                p += n;
                continue;
            }
        }
        if (strncmp(p, "https://", 8) == 0 || strncmp(p, "http://", 7) == 0)
        {
            const char *q = p;
            while (*q && *q != ' ' && *q != '\n' && *q != '\t')
                q++;
            while (q > p && strchr(".,)!?\"'", q[-1]))
                q--;
            emit(b, p, (int)(q - p), style | ST_LINK);
            p = q;
            continue;
        }
        if (*p == '@' && (strncmp(p, "@everyone", 9) == 0 || strncmp(p, "@here", 5) == 0))
        {
            int n = p[1] == 'e' ? 9 : 5;
            emit(b, p, n, style | ST_MENTION);
            p += n;
            continue;
        }
        const char *q = p + 1;
        while (*q && !strchr("\n`*_~|<\\@h", *q))
            q++;
        emit(b, p, (int)(q - p), style);
        p = q;
    }
}

static int face_for(uint16_t style)
{
    if (style & (ST_CODE | ST_BLOCK))
        return FACE_MONO;
    if (style & ST_BOLD)
        return FACE_BOLD;
    if (style & ST_ITALIC)
        return FACE_ITALIC;
    return FACE_REGULAR;
}

static int px_for(uint16_t style)
{
    return style & (ST_CODE | ST_BLOCK) ? UI_TEXT_PX - 1 : UI_TEXT_PX;
}

static void add_piece(RichLayout *l, int *cap, int start, int len, int x, int line, int width, uint16_t style)
{
    if (l->count == *cap)
    {
        int n = *cap ? *cap * 2 : 32;
        Piece *p = realloc(l->pieces, sizeof(Piece) * (size_t)n);
        if (!p)
            return;
        l->pieces = p;
        *cap = n;
    }
    l->pieces[l->count++] = (Piece){ start, len, x, line, width, style };
}

static void mark_line(bool **arr, int *cap, int line)
{
    if (line >= *cap)
    {
        int n = (line + 1) * 2;
        bool *a = realloc(*arr, sizeof(bool) * (size_t)n);
        if (!a)
            return;
        memset(a + *cap, 0, sizeof(bool) * (size_t)(n - *cap));
        *arr = a;
        *cap = n;
    }
    (*arr)[line] = true;
}

RichLayout *rich_layout(const char *text, int width, Snowflake guild_id, uint32_t base_color)
{
    RichLayout *l = calloc(1, sizeof(RichLayout));
    if (!l)
        return NULL;
    l->color = base_color;
    l->width = width;
    l->line_h = font_line_height(FACE_REGULAR, UI_TEXT_PX);
    Builder b = { 0 };
    b.guild = guild_id;
    parse(&b, text ? text : "");
    l->text = b.text.data ? b.text.data : xstrdup("");
    if (width < 40)
        width = 40;

    int cap = 0, block_cap = 0, quote_cap = 0;
    int x = 0, line = 0;
    bool any = false;
    for (int si = 0; si < b.count; si++)
    {
        Span *sp = &b.spans[si];
        int face = face_for(sp->style);
        int px = px_for(sp->style);
        int indent = sp->style & ST_QUOTE ? 14 : sp->style & ST_BLOCK ? 8 : 0;
        int avail = width - indent - (sp->style & ST_BLOCK ? 8 : 0);
        const char *s = l->text + sp->start;
        int i = 0;
        while (i < sp->len)
        {
            if (sp->style & ST_BLOCK)
                mark_line(&l->block_lines, &block_cap, line);
            if (sp->style & ST_QUOTE)
                mark_line(&l->quote_lines, &quote_cap, line);
            if (s[i] == '\n')
            {
                line++;
                x = 0;
                i++;
                any = true;
                continue;
            }
            int j = i;
            if (s[i] == ' ' || s[i] == '\t')
            {
                while (j < sp->len && (s[j] == ' ' || s[j] == '\t'))
                    j++;
                int w = font_width(face, px, s + i, j - i);
                if (x + w > avail && !(sp->style & ST_BLOCK))
                {
                    line++;
                    x = 0;
                }
                else
                {
                    if (x > 0 || (sp->style & ST_BLOCK))
                        x += w;
                }
                i = j;
                continue;
            }
            while (j < sp->len && s[j] != ' ' && s[j] != '\t' && s[j] != '\n')
                j++;
            int w = font_width(face, px, s + i, j - i);
            if (x > 0 && x + w > avail)
            {
                line++;
                x = 0;
                if (sp->style & ST_BLOCK)
                    mark_line(&l->block_lines, &block_cap, line);
                if (sp->style & ST_QUOTE)
                    mark_line(&l->quote_lines, &quote_cap, line);
            }
            while (w > avail - x && j - i > 1)
            {
                int fit = font_fit(face, px, s + i, j - i, avail - x);
                if (fit <= 0)
                    fit = 1;
                int fw = font_width(face, px, s + i, fit);
                add_piece(l, &cap, sp->start + i, fit, indent + x, line, fw, sp->style);
                i += fit;
                line++;
                x = 0;
                if (sp->style & ST_BLOCK)
                    mark_line(&l->block_lines, &block_cap, line);
                if (sp->style & ST_QUOTE)
                    mark_line(&l->quote_lines, &quote_cap, line);
                w = font_width(face, px, s + i, j - i);
            }
            add_piece(l, &cap, sp->start + i, j - i, indent + x, line, w, sp->style);
            x += w;
            i = j;
            any = true;
        }
    }
    l->lines = (any || l->count) ? line + 1 : 0;
    while (l->lines > 0)
    {
        bool used = false;
        for (int k = 0; k < l->count; k++)
            if (l->pieces[k].line == l->lines - 1)
                used = true;
        bool block = l->block_lines && l->lines - 1 < block_cap && l->block_lines[l->lines - 1];
        if (used || block)
            break;
        l->lines--;
    }
    if (!l->block_lines)
        l->block_lines = calloc(1, sizeof(bool));
    if (l->lines >= block_cap)
    {
        bool *a = realloc(l->block_lines, sizeof(bool) * (size_t)(l->lines + 1));
        if (a)
        {
            memset(a + block_cap, 0, sizeof(bool) * (size_t)(l->lines + 1 - block_cap));
            l->block_lines = a;
        }
    }
    if (!l->quote_lines || l->lines >= quote_cap)
    {
        bool *a = realloc(l->quote_lines, sizeof(bool) * (size_t)(l->lines + 1));
        if (a)
        {
            memset(a + quote_cap, 0, sizeof(bool) * (size_t)(l->lines + 1 - quote_cap));
            l->quote_lines = a;
        }
    }
    free(b.spans);
    return l;
}

int rich_height(const RichLayout *l)
{
    return l ? l->lines * l->line_h : 0;
}

void rich_draw(SxCanvas *c, const RichLayout *l, int x, int y)
{
    if (!l)
        return;
    int lh = l->line_h;
    SxRect clip = c->clip;
    for (int line = 0; line < l->lines; line++)
    {
        int ly = y + line * lh;
        if (ly > clip.y + clip.h || ly + lh < clip.y)
            continue;
        if (l->block_lines && l->block_lines[line])
            sx_fill_rect(c, x, ly, l->width, lh, COL_CODE_BG);
        if (l->quote_lines && l->quote_lines[line])
            sx_fill_rect(c, x, ly, 4, lh, SX_RGB(0x4E, 0x50, 0x58));
    }
    int asc = font_ascent(FACE_REGULAR, UI_TEXT_PX);
    for (int i = 0; i < l->count; i++)
    {
        const Piece *p = &l->pieces[i];
        int py = y + p->line * lh;
        if (py > clip.y + clip.h || py + lh < clip.y)
            continue;
        int px = x + p->x;
        uint32_t color = l->color;
        if (p->style & ST_LINK)
            color = COL_LINK;
        if (p->style & ST_MENTION)
        {
            sx_fill_rounded_rect(c, px - 2, py + 1, p->width + 4, lh - 2, 3, COL_MENTION_BG);
            color = COL_MENTION;
        }
        if (p->style & ST_SPOILER)
            sx_fill_rounded_rect(c, px - 2, py + 1, p->width + 4, lh - 2, 3, COL_GUILDBAR);
        if ((p->style & ST_CODE) && !(p->style & ST_BLOCK))
            sx_fill_rounded_rect(c, px - 2, py + 1, p->width + 4, lh - 2, 3, COL_CODE_BG);
        font_draw(c, face_for(p->style), px_for(p->style), px, py + asc + 1, l->text + p->start, p->len, color);
        if (p->style & ST_STRIKE)
            sx_fill_rect(c, px, py + lh / 2 + 1, p->width, 1, color);
        if (p->style & ST_LINK)
            sx_fill_rect(c, px, py + asc + 3, p->width, 1, (COL_LINK & 0x00FFFFFFu) | 0x50000000u);
        else if (p->style & ST_UNDER)
            sx_fill_rect(c, px, py + asc + 3, p->width, 1, color);
    }
}

void rich_free(RichLayout *l)
{
    if (!l)
        return;
    free(l->text);
    free(l->pieces);
    free(l->block_lines);
    free(l->quote_lines);
    free(l);
}
