#include <stdlib.h>
#include <string.h>

#include "discord.h"

#define ARENA_CHUNK (256 * 1024)
#define MAX_DEPTH   192

typedef struct Chunk Chunk;

struct Chunk
{
    Chunk *next;
    size_t used;
    size_t size;
    char   data[];
};

struct JsonDoc
{
    Chunk *chunks;
    Json  *root;
};

typedef struct
{
    JsonDoc    *doc;
    const char *p;
    const char *end;
    int         depth;
    bool        failed;
}
Parser;

static void *arena_alloc(JsonDoc *doc, size_t size)
{
    size = (size + 7) & ~(size_t)7;
    Chunk *c = doc->chunks;
    if (!c || c->used + size > c->size)
    {
        size_t cap = size > ARENA_CHUNK ? size : ARENA_CHUNK;
        c = malloc(sizeof(Chunk) + cap);
        if (!c)
            return NULL;
        c->used = 0;
        c->size = cap;
        c->next = doc->chunks;
        doc->chunks = c;
    }
    void *p = c->data + c->used;
    c->used += size;
    return p;
}

static void skip_ws(Parser *ps)
{
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
        ps->p++;
}

static int hex4(const char *s)
{
    int v = 0;
    for (int i = 0; i < 4; i++)
    {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static int put_utf8(char *out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18);
    out[1] = (char)(0x80 | (cp >> 12 & 0x3F));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static const char *parse_string(Parser *ps)
{
    ps->p++;
    const char *start = ps->p;
    const char *q = start;
    bool escaped = false;
    while (q < ps->end && *q != '"')
    {
        if (*q == '\\')
        {
            escaped = true;
            q++;
        }
        q++;
    }
    if (q >= ps->end)
    {
        ps->failed = true;
        return NULL;
    }
    size_t raw = (size_t)(q - start);
    char *out = arena_alloc(ps->doc, raw + 1);
    if (!out)
    {
        ps->failed = true;
        return NULL;
    }
    if (!escaped)
    {
        memcpy(out, start, raw);
        out[raw] = '\0';
        ps->p = q + 1;
        return out;
    }
    size_t o = 0;
    const char *s = start;
    while (s < q)
    {
        if (*s != '\\')
        {
            out[o++] = *s++;
            continue;
        }
        s++;
        char e = *s++;
        switch (e)
        {
            case 'n': out[o++] = '\n'; break;
            case 't': out[o++] = '\t'; break;
            case 'r': out[o++] = '\r'; break;
            case 'b': out[o++] = '\b'; break;
            case 'f': out[o++] = '\f'; break;
            case 'u':
            {
                if (q - s < 4)
                {
                    ps->failed = true;
                    return NULL;
                }
                int v = hex4(s);
                s += 4;
                if (v < 0)
                    v = 0xFFFD;
                uint32_t cp = (uint32_t)v;
                if (cp >= 0xD800 && cp < 0xDC00 && q - s >= 6 && s[0] == '\\' && s[1] == 'u')
                {
                    int lo = hex4(s + 2);
                    if (lo >= 0xDC00 && lo < 0xE000)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
                        s += 6;
                    }
                }
                if (cp >= 0xD800 && cp < 0xE000)
                    cp = 0xFFFD;
                o += (size_t)put_utf8(out + o, cp);
                break;
            }
            default:
                out[o++] = e;
        }
    }
    out[o] = '\0';
    ps->p = q + 1;
    return out;
}

static Json *parse_value(Parser *ps);

static Json *new_node(Parser *ps, JsonType type)
{
    Json *n = arena_alloc(ps->doc, sizeof(Json));
    if (!n)
    {
        ps->failed = true;
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    n->type = type;
    return n;
}

static Json *parse_container(Parser *ps, bool object)
{
    Json *node = new_node(ps, object ? JSON_OBJECT : JSON_ARRAY);
    if (!node)
        return NULL;
    if (++ps->depth > MAX_DEPTH)
    {
        ps->failed = true;
        return NULL;
    }
    ps->p++;
    Json *tail = NULL;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == (object ? '}' : ']'))
    {
        ps->p++;
        ps->depth--;
        return node;
    }
    for (;;)
    {
        skip_ws(ps);
        const char *key = NULL;
        if (object)
        {
            if (ps->p >= ps->end || *ps->p != '"')
            {
                ps->failed = true;
                return NULL;
            }
            key = parse_string(ps);
            if (!key)
                return NULL;
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':')
            {
                ps->failed = true;
                return NULL;
            }
            ps->p++;
        }
        Json *v = parse_value(ps);
        if (!v)
            return NULL;
        v->key = key;
        if (tail)
            tail->next = v;
        else
            node->child = v;
        tail = v;
        node->count++;
        skip_ws(ps);
        if (ps->p >= ps->end)
        {
            ps->failed = true;
            return NULL;
        }
        if (*ps->p == ',')
        {
            ps->p++;
            continue;
        }
        if (*ps->p == (object ? '}' : ']'))
        {
            ps->p++;
            break;
        }
        ps->failed = true;
        return NULL;
    }
    ps->depth--;
    return node;
}

static bool match(Parser *ps, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0)
        return false;
    ps->p += n;
    return true;
}

static Json *parse_value(Parser *ps)
{
    skip_ws(ps);
    if (ps->failed || ps->p >= ps->end)
    {
        ps->failed = true;
        return NULL;
    }
    char c = *ps->p;
    if (c == '{')
        return parse_container(ps, true);
    if (c == '[')
        return parse_container(ps, false);
    if (c == '"')
    {
        const char *s = parse_string(ps);
        if (!s)
            return NULL;
        Json *n = new_node(ps, JSON_STRING);
        if (n)
            n->str = s;
        return n;
    }
    if (match(ps, "true"))
        return new_node(ps, JSON_TRUE);
    if (match(ps, "false"))
        return new_node(ps, JSON_FALSE);
    if (match(ps, "null"))
        return new_node(ps, JSON_NULL);
    if (c == '-' || (c >= '0' && c <= '9'))
    {
        const char *start = ps->p;
        while (ps->p < ps->end && (strchr("+-.eE", *ps->p) || (*ps->p >= '0' && *ps->p <= '9')))
            ps->p++;
        size_t len = (size_t)(ps->p - start);
        char tmp[64];
        if (len >= sizeof(tmp))
            len = sizeof(tmp) - 1;
        memcpy(tmp, start, len);
        tmp[len] = '\0';
        Json *n = new_node(ps, JSON_NUMBER);
        if (!n)
            return NULL;
        n->num = strtod(tmp, NULL);
        char *s = arena_alloc(ps->doc, len + 1);
        if (s)
        {
            memcpy(s, tmp, len + 1);
            n->str = s;
        }
        return n;
    }
    ps->failed = true;
    return NULL;
}

void json_free(JsonDoc *doc)
{
    if (!doc)
        return;
    Chunk *c = doc->chunks;
    while (c)
    {
        Chunk *next = c->next;
        free(c);
        c = next;
    }
    free(doc);
}

JsonDoc *json_parse(const char *text, size_t len)
{
    JsonDoc *doc = calloc(1, sizeof(JsonDoc));
    if (!doc)
        return NULL;
    Parser ps = { doc, text, text + len, 0, false };
    doc->root = parse_value(&ps);
    if (ps.failed || !doc->root)
    {
        json_free(doc);
        return NULL;
    }
    return doc;
}

Json *json_root(JsonDoc *doc)
{
    return doc ? doc->root : NULL;
}

Json *json_get(const Json *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT)
        return NULL;
    for (Json *c = obj->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0)
            return c;
    return NULL;
}

const char *json_str(const Json *obj, const char *key)
{
    Json *v = json_get(obj, key);
    return v && v->type == JSON_STRING ? v->str : NULL;
}

int64_t json_int(const Json *obj, const char *key, int64_t def)
{
    Json *v = json_get(obj, key);
    if (!v)
        return def;
    if (v->type == JSON_NUMBER)
        return v->str ? strtoll(v->str, NULL, 10) : (int64_t)v->num;
    if (v->type == JSON_STRING)
        return strtoll(v->str, NULL, 10);
    return def;
}

bool json_bool(const Json *obj, const char *key)
{
    Json *v = json_get(obj, key);
    return v && v->type == JSON_TRUE;
}

Snowflake json_as_id(const Json *v)
{
    if (!v)
        return 0;
    if (v->type == JSON_STRING || (v->type == JSON_NUMBER && v->str))
        return parse_snowflake(v->str);
    return 0;
}

Snowflake json_id(const Json *obj, const char *key)
{
    return json_as_id(json_get(obj, key));
}
