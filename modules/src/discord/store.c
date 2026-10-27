#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discord.h"

#define PERM_ADMINISTRATOR (1ULL << 3)
#define PERM_VIEW_CHANNEL  (1ULL << 10)

Store S;

static User   **user_table;
static size_t   user_cap;
static size_t   user_count;
static uint64_t pending_counter;

static void user_free_fields(User *u)
{
    free(u->username);
    free(u->global_name);
    free(u->avatar);
    free(u->discriminator);
}

User *store_user(Snowflake id)
{
    if (!user_table || !id)
        return NULL;
    size_t i = (size_t)(id * 0x9E3779B97F4A7C15ULL) & (user_cap - 1);
    while (user_table[i])
    {
        if (user_table[i]->id == id)
            return user_table[i];
        i = (i + 1) & (user_cap - 1);
    }
    return NULL;
}

static void user_insert(User *u)
{
    if ((user_count + 1) * 2 > user_cap)
    {
        size_t cap = user_cap ? user_cap * 2 : 1024;
        User **t = calloc(cap, sizeof(User *));
        if (!t)
            return;
        for (size_t k = 0; k < user_cap; k++)
        {
            User *e = user_table[k];
            if (!e)
                continue;
            size_t i = (size_t)(e->id * 0x9E3779B97F4A7C15ULL) & (cap - 1);
            while (t[i])
                i = (i + 1) & (cap - 1);
            t[i] = e;
        }
        free(user_table);
        user_table = t;
        user_cap = cap;
    }
    size_t i = (size_t)(u->id * 0x9E3779B97F4A7C15ULL) & (user_cap - 1);
    while (user_table[i])
        i = (i + 1) & (user_cap - 1);
    user_table[i] = u;
    user_count++;
}

static void set_str(char **dst, const char *src)
{
    if (!src)
        return;
    if (*dst && strcmp(*dst, src) == 0)
        return;
    free(*dst);
    *dst = xstrdup(src);
}

static User *user_update(Json *j)
{
    Snowflake id = json_id(j, "id");
    if (!id)
        return NULL;
    User *u = store_user(id);
    if (!u)
    {
        u = calloc(1, sizeof(User));
        if (!u)
            return NULL;
        u->id = id;
        user_insert(u);
    }
    set_str(&u->username, json_str(j, "username"));
    Json *gn = json_get(j, "global_name");
    if (gn && gn->type == JSON_STRING)
        set_str(&u->global_name, gn->str);
    Json *av = json_get(j, "avatar");
    if (av && av->type == JSON_STRING)
        set_str(&u->avatar, av->str);
    else if (av && av->type == JSON_NULL)
    {
        free(u->avatar);
        u->avatar = NULL;
    }
    set_str(&u->discriminator, json_str(j, "discriminator"));
    return u;
}

void user_avatar_url(Snowflake id, const char *avatar, const char *disc, char *out, size_t size)
{
    if (avatar && *avatar)
    {
        snprintf(out, size, "%s/avatars/%llu/%s.png?size=80", cdn_base, (unsigned long long)id, avatar);
        return;
    }
    int index = (!disc || strcmp(disc, "0") == 0) ? (int)((id >> 22) % 6) : atoi(disc) % 5;
    snprintf(out, size, "%s/embed/avatars/%d.png", cdn_base, index);
}

void message_free(Message *m)
{
    if (!m)
        return;
    free(m->author_name);
    free(m->author_avatar);
    free(m->author_disc);
    free(m->content);
    free(m->nonce);
    for (int i = 0; i < m->attachment_count; i++)
    {
        free(m->attachments[i].url);
        free(m->attachments[i].proxy_url);
        free(m->attachments[i].filename);
        free(m->attachments[i].content_type);
    }
    free(m->attachments);
    for (int i = 0; i < m->embed_count; i++)
    {
        free(m->embeds[i].title);
        free(m->embeds[i].description);
        free(m->embeds[i].author);
        free(m->embeds[i].url);
        free(m->embeds[i].image);
    }
    free(m->embeds);
    free(m->reply_author);
    free(m->reply_content);
    rich_free(m->layout);
    for (int i = 0; i < 4; i++)
        rich_free(m->embed_layouts[i]);
    free(m);
}

static void channel_free(Channel *ch)
{
    if (!ch)
        return;
    free(ch->name);
    free(ch->topic);
    free(ch->icon);
    free(ch->recipients);
    free(ch->overwrites);
    for (int i = 0; i < ch->message_count; i++)
        message_free(ch->messages[i]);
    free(ch->messages);
    free(ch);
}

static void guild_free(Guild *g)
{
    if (!g)
        return;
    free(g->name);
    free(g->icon);
    for (int i = 0; i < g->channel_count; i++)
        channel_free(g->channels[i]);
    free(g->channels);
    for (int i = 0; i < g->role_count; i++)
        free(g->roles[i].name);
    free(g->roles);
    free(g->my_roles);
    free(g);
}

void store_reset(void)
{
    for (int i = 0; i < S.guild_count; i++)
        guild_free(S.guilds[i]);
    free(S.guilds);
    for (int i = 0; i < S.dm_count; i++)
        channel_free(S.dms[i]);
    free(S.dms);
    user_free_fields(&S.me);
    for (size_t i = 0; i < user_cap; i++)
        if (user_table[i])
        {
            user_free_fields(user_table[i]);
            free(user_table[i]);
        }
    free(user_table);
    user_table = NULL;
    user_cap = user_count = 0;
    int version = S.version;
    memset(&S, 0, sizeof(S));
    S.version = version + 1;
}

Guild *store_guild(Snowflake id)
{
    for (int i = 0; i < S.guild_count; i++)
        if (S.guilds[i]->id == id)
            return S.guilds[i];
    return NULL;
}

Channel *store_channel(Snowflake id)
{
    for (int i = 0; i < S.dm_count; i++)
        if (S.dms[i]->id == id)
            return S.dms[i];
    for (int i = 0; i < S.guild_count; i++)
    {
        Guild *g = S.guilds[i];
        for (int k = 0; k < g->channel_count; k++)
            if (g->channels[k]->id == id)
                return g->channels[k];
    }
    return NULL;
}

static uint64_t parse_perm(Json *v)
{
    if (!v)
        return 0;
    if (v->type == JSON_STRING || v->type == JSON_NUMBER)
        return v->str ? strtoull(v->str, NULL, 10) : (uint64_t)v->num;
    return 0;
}

static void channel_fill(Channel *ch, Json *j)
{
    ch->id = json_id(j, "id");
    ch->type = (int)json_int(j, "type", ch->type);
    Snowflake gid = json_id(j, "guild_id");
    if (gid)
        ch->guild_id = gid;
    ch->parent_id = json_id(j, "parent_id");
    ch->position = (int)json_int(j, "position", ch->position);
    set_str(&ch->name, json_str(j, "name"));
    Json *topic = json_get(j, "topic");
    if (topic)
    {
        free(ch->topic);
        ch->topic = topic->type == JSON_STRING ? xstrdup(topic->str) : NULL;
    }
    Snowflake last = json_id(j, "last_message_id");
    if (last)
        ch->last_message_id = last;
    set_str(&ch->icon, json_str(j, "icon"));

    Json *ow = json_get(j, "permission_overwrites");
    if (ow && ow->type == JSON_ARRAY)
    {
        free(ch->overwrites);
        ch->overwrites = calloc((size_t)ow->count + 1, sizeof(Overwrite));
        ch->overwrite_count = 0;
        for (Json *o = ow->child; o && ch->overwrites; o = o->next)
        {
            Overwrite *w = &ch->overwrites[ch->overwrite_count++];
            w->id = json_id(o, "id");
            Json *t = json_get(o, "type");
            w->type = t && t->type == JSON_STRING ? strcmp(t->str, "member") == 0 || strcmp(t->str, "1") == 0 : (int)json_int(o, "type", 0);
            w->allow = parse_perm(json_get(o, "allow"));
            w->deny = parse_perm(json_get(o, "deny"));
        }
    }

    Json *rec = json_get(j, "recipients");
    Json *rec_ids = json_get(j, "recipient_ids");
    Json *list = rec && rec->type == JSON_ARRAY ? rec : rec_ids && rec_ids->type == JSON_ARRAY ? rec_ids : NULL;
    if (list)
    {
        free(ch->recipients);
        ch->recipients = calloc((size_t)list->count + 1, sizeof(Snowflake));
        ch->recipient_count = 0;
        for (Json *r = list->child; r && ch->recipients; r = r->next)
        {
            Snowflake id;
            if (r->type == JSON_OBJECT)
            {
                User *u = user_update(r);
                id = u ? u->id : 0;
            }
            else
            {
                id = json_as_id(r);
            }
            if (id)
                ch->recipients[ch->recipient_count++] = id;
        }
    }
}

static Channel *channel_new(Json *j, Snowflake guild_id)
{
    Channel *ch = calloc(1, sizeof(Channel));
    if (!ch)
        return NULL;
    ch->guild_id = guild_id;
    ch->has_more = true;
    channel_fill(ch, j);
    return ch;
}

static int channel_order(const void *a, const void *b)
{
    const Channel *x = *(Channel *const *)a;
    const Channel *y = *(Channel *const *)b;
    bool xv = x->type == CH_VOICE || x->type == CH_STAGE;
    bool yv = y->type == CH_VOICE || y->type == CH_STAGE;
    if (xv != yv)
        return xv ? 1 : -1;
    if (x->position != y->position)
        return x->position < y->position ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

static void guild_sort_channels(Guild *g)
{
    qsort(g->channels, (size_t)g->channel_count, sizeof(Channel *), channel_order);
}

static void guild_add_channel(Guild *g, Channel *ch)
{
    Channel **n = realloc(g->channels, sizeof(Channel *) * (size_t)(g->channel_count + 1));
    if (!n)
    {
        channel_free(ch);
        return;
    }
    g->channels = n;
    g->channels[g->channel_count++] = ch;
}

static void guild_set_member(Guild *g, Json *member)
{
    Json *roles = json_get(member, "roles");
    if (!roles || roles->type != JSON_ARRAY)
        return;
    free(g->my_roles);
    g->my_roles = calloc((size_t)roles->count + 1, sizeof(Snowflake));
    g->my_role_count = 0;
    for (Json *r = roles->child; r && g->my_roles; r = r->next)
        g->my_roles[g->my_role_count++] = json_as_id(r);
    g->have_member = true;
}

static Guild *guild_parse(Json *j, Json *merged_member)
{
    if (json_bool(j, "unavailable"))
        return NULL;
    Json *props = json_get(j, "properties");
    Json *src = props && props->type == JSON_OBJECT ? props : j;
    Guild *g = calloc(1, sizeof(Guild));
    if (!g)
        return NULL;
    g->id = json_id(j, "id");
    g->name = xstrdup(json_str(src, "name") ? json_str(src, "name") : "Server");
    g->icon = xstrdup(json_str(src, "icon"));
    g->owner_id = json_id(src, "owner_id");

    Json *roles = json_get(j, "roles");
    if (roles && roles->type == JSON_ARRAY)
    {
        g->roles = calloc((size_t)roles->count + 1, sizeof(Role));
        for (Json *r = roles->child; r && g->roles; r = r->next)
        {
            Role *role = &g->roles[g->role_count++];
            role->id = json_id(r, "id");
            role->permissions = parse_perm(json_get(r, "permissions"));
            role->position = (int)json_int(r, "position", 0);
            role->color = (uint32_t)json_int(r, "color", 0);
            role->name = xstrdup(json_str(r, "name"));
        }
    }

    Json *chans = json_get(j, "channels");
    if (chans && chans->type == JSON_ARRAY)
        for (Json *c = chans->child; c; c = c->next)
        {
            Channel *ch = channel_new(c, g->id);
            if (ch)
                guild_add_channel(g, ch);
        }
    guild_sort_channels(g);

    if (merged_member)
        guild_set_member(g, merged_member);
    Json *members = json_get(j, "members");
    if (!g->have_member && members && members->type == JSON_ARRAY)
        for (Json *m = members->child; m; m = m->next)
        {
            Json *user = json_get(m, "user");
            Snowflake uid = user ? json_id(user, "id") : json_id(m, "user_id");
            if (uid == S.me.id)
            {
                guild_set_member(g, m);
                break;
            }
        }
    return g;
}

static void guild_store(Guild *g, int index_hint)
{
    for (int i = 0; i < S.guild_count; i++)
        if (S.guilds[i]->id == g->id)
        {
            Guild *old = S.guilds[i];
            for (int k = 0; k < g->channel_count; k++)
            {
                Channel *nc = g->channels[k];
                for (int o = 0; o < old->channel_count; o++)
                {
                    Channel *oc = old->channels[o];
                    if (oc && oc->id == nc->id)
                    {
                        nc->messages = oc->messages;
                        nc->message_count = oc->message_count;
                        nc->message_cap = oc->message_cap;
                        nc->loaded = oc->loaded;
                        nc->has_more = oc->has_more;
                        oc->messages = NULL;
                        oc->message_count = 0;
                    }
                }
            }
            S.guilds[i] = g;
            guild_free(old);
            return;
        }
    Guild **n = realloc(S.guilds, sizeof(Guild *) * (size_t)(S.guild_count + 1));
    if (!n)
    {
        guild_free(g);
        return;
    }
    S.guilds = n;
    if (index_hint < 0 || index_hint > S.guild_count)
        index_hint = S.guild_count;
    memmove(&S.guilds[index_hint + 1], &S.guilds[index_hint], sizeof(Guild *) * (size_t)(S.guild_count - index_hint));
    S.guilds[index_hint] = g;
    S.guild_count++;
}

static int dm_order(const void *a, const void *b)
{
    const Channel *x = *(Channel *const *)a;
    const Channel *y = *(Channel *const *)b;
    Snowflake lx = x->last_message_id ? x->last_message_id : x->id;
    Snowflake ly = y->last_message_id ? y->last_message_id : y->id;
    return lx > ly ? -1 : lx < ly;
}

static void dm_add(Channel *ch)
{
    Channel **n = realloc(S.dms, sizeof(Channel *) * (size_t)(S.dm_count + 1));
    if (!n)
    {
        channel_free(ch);
        return;
    }
    S.dms = n;
    S.dms[S.dm_count++] = ch;
}

static void order_guilds(Json *settings)
{
    Json *folders = json_get(settings, "guild_folders");
    Json *positions = json_get(settings, "guild_positions");
    Snowflake order[1024];
    int n = 0;
    if (folders && folders->type == JSON_ARRAY)
    {
        for (Json *f = folders->child; f; f = f->next)
        {
            Json *ids = json_get(f, "guild_ids");
            if (ids && ids->type == JSON_ARRAY)
                for (Json *id = ids->child; id && n < 1024; id = id->next)
                    order[n++] = json_as_id(id);
        }
    }
    else if (positions && positions->type == JSON_ARRAY)
    {
        for (Json *id = positions->child; id && n < 1024; id = id->next)
            order[n++] = json_as_id(id);
    }
    int placed = 0;
    for (int i = 0; i < n; i++)
        for (int k = placed; k < S.guild_count; k++)
            if (S.guilds[k]->id == order[i])
            {
                Guild *t = S.guilds[placed];
                S.guilds[placed] = S.guilds[k];
                S.guilds[k] = t;
                placed++;
                break;
            }
}

void store_on_ready(Json *d)
{
    Json *me = json_get(d, "user");
    if (me)
    {
        S.me.id = json_id(me, "id");
        set_str(&S.me.username, json_str(me, "username"));
        set_str(&S.me.global_name, json_str(me, "global_name"));
        set_str(&S.me.avatar, json_str(me, "avatar"));
        set_str(&S.me.discriminator, json_str(me, "discriminator"));
        user_update(me);
    }

    Json *users = json_get(d, "users");
    if (users && users->type == JSON_ARRAY)
        for (Json *u = users->child; u; u = u->next)
            user_update(u);

    Json *guilds = json_get(d, "guilds");
    Json *merged = json_get(d, "merged_members");
    Json *mm = merged && merged->type == JSON_ARRAY ? merged->child : NULL;
    if (guilds && guilds->type == JSON_ARRAY)
        for (Json *g = guilds->child; g; g = g->next)
        {
            Json *mine = NULL;
            if (mm && mm->type == JSON_ARRAY)
                for (Json *m = mm->child; m; m = m->next)
                    if (json_id(m, "user_id") == S.me.id)
                        mine = m;
            Guild *parsed = guild_parse(g, mine);
            if (parsed)
                guild_store(parsed, -1);
            if (mm)
                mm = mm->next;
        }

    Json *settings = json_get(d, "user_settings");
    if (settings && settings->type == JSON_OBJECT)
        order_guilds(settings);

    Json *pcs = json_get(d, "private_channels");
    if (pcs && pcs->type == JSON_ARRAY)
        for (Json *c = pcs->child; c; c = c->next)
        {
            Channel *ch = channel_new(c, 0);
            if (ch)
                dm_add(ch);
        }
    qsort(S.dms, (size_t)S.dm_count, sizeof(Channel *), dm_order);
    S.ready = true;
    S.version++;
}

void store_on_guild_create(Json *g)
{
    Guild *parsed = guild_parse(g, NULL);
    if (parsed)
        guild_store(parsed, -1);
    S.version++;
}

void store_on_guild_delete(Json *g)
{
    if (json_bool(g, "unavailable"))
        return;
    Snowflake id = json_id(g, "id");
    for (int i = 0; i < S.guild_count; i++)
        if (S.guilds[i]->id == id)
        {
            guild_free(S.guilds[i]);
            memmove(&S.guilds[i], &S.guilds[i + 1], sizeof(Guild *) * (size_t)(S.guild_count - i - 1));
            S.guild_count--;
            break;
        }
    S.version++;
}

void store_on_channel(Json *c, bool deleted)
{
    Snowflake id = json_id(c, "id");
    Snowflake gid = json_id(c, "guild_id");
    Channel *existing = store_channel(id);
    if (deleted)
    {
        if (!existing)
            return;
        if (existing->guild_id)
        {
            Guild *g = store_guild(existing->guild_id);
            for (int i = 0; g && i < g->channel_count; i++)
                if (g->channels[i] == existing)
                {
                    memmove(&g->channels[i], &g->channels[i + 1], sizeof(Channel *) * (size_t)(g->channel_count - i - 1));
                    g->channel_count--;
                    break;
                }
        }
        else
        {
            for (int i = 0; i < S.dm_count; i++)
                if (S.dms[i] == existing)
                {
                    memmove(&S.dms[i], &S.dms[i + 1], sizeof(Channel *) * (size_t)(S.dm_count - i - 1));
                    S.dm_count--;
                    break;
                }
        }
        channel_free(existing);
        S.version++;
        return;
    }
    if (existing)
    {
        channel_fill(existing, c);
        Guild *g = store_guild(existing->guild_id);
        if (g)
            guild_sort_channels(g);
    }
    else if (gid)
    {
        Guild *g = store_guild(gid);
        Channel *ch = g ? channel_new(c, gid) : NULL;
        if (ch)
        {
            guild_add_channel(g, ch);
            guild_sort_channels(g);
        }
    }
    else
    {
        Channel *ch = channel_new(c, 0);
        if (ch)
        {
            dm_add(ch);
            qsort(S.dms, (size_t)S.dm_count, sizeof(Channel *), dm_order);
        }
    }
    S.version++;
}

bool store_channel_is_text(const Channel *ch)
{
    return ch->type == CH_TEXT || ch->type == CH_ANNOUNCEMENT || ch->type == CH_DM || ch->type == CH_GROUP_DM;
}

bool store_channel_visible(Guild *g, Channel *ch)
{
    if (!g || !g->have_member || g->owner_id == S.me.id)
        return true;
    uint64_t perms = 0;
    for (int i = 0; i < g->role_count; i++)
    {
        if (g->roles[i].id == g->id)
            perms |= g->roles[i].permissions;
        for (int k = 0; k < g->my_role_count; k++)
            if (g->roles[i].id == g->my_roles[k])
                perms |= g->roles[i].permissions;
    }
    if (perms & PERM_ADMINISTRATOR)
        return true;
    uint64_t allow = 0, deny = 0;
    for (int i = 0; i < ch->overwrite_count; i++)
        if (ch->overwrites[i].id == g->id)
        {
            perms &= ~ch->overwrites[i].deny;
            perms |= ch->overwrites[i].allow;
        }
    for (int i = 0; i < ch->overwrite_count; i++)
    {
        Overwrite *o = &ch->overwrites[i];
        if (o->type != 0)
            continue;
        for (int k = 0; k < g->my_role_count; k++)
            if (o->id == g->my_roles[k])
            {
                allow |= o->allow;
                deny |= o->deny;
            }
    }
    perms &= ~deny;
    perms |= allow;
    for (int i = 0; i < ch->overwrite_count; i++)
        if (ch->overwrites[i].type == 1 && ch->overwrites[i].id == S.me.id)
        {
            perms &= ~ch->overwrites[i].deny;
            perms |= ch->overwrites[i].allow;
        }
    return (perms & PERM_VIEW_CHANNEL) != 0;
}

static const char *user_display(const User *u)
{
    if (!u)
        return "Unknown";
    if (u->global_name && *u->global_name)
        return u->global_name;
    return u->username ? u->username : "Unknown";
}

const char *store_user_name(Snowflake id)
{
    User *u = store_user(id);
    return u ? user_display(u) : NULL;
}

const char *store_role_name(Snowflake guild_id, Snowflake role_id)
{
    Guild *g = store_guild(guild_id);
    for (int i = 0; g && i < g->role_count; i++)
        if (g->roles[i].id == role_id)
            return g->roles[i].name;
    return NULL;
}

void store_channel_title(const Channel *ch, char *out, size_t size)
{
    if (ch->type == CH_DM || ch->type == CH_GROUP_DM)
    {
        if (ch->name && *ch->name)
        {
            snprintf(out, size, "%s", ch->name);
            return;
        }
        out[0] = '\0';
        size_t used = 0;
        for (int i = 0; i < ch->recipient_count && used + 4 < size; i++)
        {
            const char *name = user_display(store_user(ch->recipients[i]));
            int n = snprintf(out + used, size - used, "%s%s", i ? ", " : "", name);
            if (n < 0)
                break;
            used += (size_t)n;
        }
        if (!out[0])
            snprintf(out, size, "Direct Message");
        return;
    }
    snprintf(out, size, "%s", ch->name ? ch->name : "channel");
}

static uint32_t member_color(Guild *g, Json *member)
{
    Json *roles = member ? json_get(member, "roles") : NULL;
    if (!g || !roles || roles->type != JSON_ARRAY)
        return 0;
    int best = -1;
    uint32_t color = 0;
    for (Json *r = roles->child; r; r = r->next)
    {
        Snowflake id = json_as_id(r);
        for (int i = 0; i < g->role_count; i++)
            if (g->roles[i].id == id && g->roles[i].color && g->roles[i].position > best)
            {
                best = g->roles[i].position;
                color = g->roles[i].color;
            }
    }
    return color;
}

static char *display_name(Json *author, Json *member)
{
    const char *nick = member ? json_str(member, "nick") : NULL;
    if (nick && *nick)
        return xstrdup(nick);
    const char *gn = json_str(author, "global_name");
    if (gn && *gn)
        return xstrdup(gn);
    const char *un = json_str(author, "username");
    return xstrdup(un ? un : "Unknown");
}

static void parse_embeds(Message *m, Json *embeds)
{
    for (int i = 0; i < 4; i++)
    {
        rich_free(m->embed_layouts[i]);
        m->embed_layouts[i] = NULL;
    }
    for (int i = 0; i < m->embed_count; i++)
    {
        free(m->embeds[i].title);
        free(m->embeds[i].description);
        free(m->embeds[i].author);
        free(m->embeds[i].url);
        free(m->embeds[i].image);
    }
    free(m->embeds);
    m->embeds = NULL;
    m->embed_count = 0;
    if (!embeds || embeds->type != JSON_ARRAY || embeds->count == 0)
        return;
    m->embeds = calloc((size_t)embeds->count, sizeof(Embed));
    for (Json *e = embeds->child; e && m->embeds; e = e->next)
    {
        Embed *em = &m->embeds[m->embed_count];
        const char *type = json_str(e, "type");
        em->title = xstrdup(json_str(e, "title"));
        em->description = xstrdup(json_str(e, "description"));
        em->url = xstrdup(json_str(e, "url"));
        Json *author = json_get(e, "author");
        if (!author)
            author = json_get(e, "provider");
        em->author = xstrdup(author ? json_str(author, "name") : NULL);
        em->color = (uint32_t)json_int(e, "color", 0);
        Json *img = json_get(e, "image");
        if (!img && type && (strcmp(type, "image") == 0 || strcmp(type, "gifv") == 0 || strcmp(type, "video") == 0 || strcmp(type, "article") == 0))
            img = json_get(e, "thumbnail");
        if (img)
        {
            const char *u = json_str(img, "proxy_url");
            em->image = xstrdup(u ? u : json_str(img, "url"));
            em->image_w = (int)json_int(img, "width", 0);
            em->image_h = (int)json_int(img, "height", 0);
        }
        if (!em->title && !em->description && !em->image && !em->author)
            continue;
        m->embed_count++;
    }
}

static Message *message_parse(Json *j)
{
    Message *m = calloc(1, sizeof(Message));
    if (!m)
        return NULL;
    m->id = json_id(j, "id");
    m->channel_id = json_id(j, "channel_id");
    m->type = (int)json_int(j, "type", 0);
    m->content = xstrdup(json_str(j, "content") ? json_str(j, "content") : "");
    m->timestamp = parse_iso8601(json_str(j, "timestamp"));
    m->edited = json_str(j, "edited_timestamp") != NULL;
    m->nonce = xstrdup(json_str(j, "nonce"));

    Json *author = json_get(j, "author");
    Json *member = json_get(j, "member");
    if (author)
    {
        user_update(author);
        m->author_id = json_id(author, "id");
        m->author_name = display_name(author, member);
        m->author_avatar = xstrdup(json_str(author, "avatar"));
        m->author_disc = xstrdup(json_str(author, "discriminator"));
        m->author_bot = json_bool(author, "bot");
    }
    else
    {
        m->author_name = xstrdup("Unknown");
    }
    Channel *ch = store_channel(m->channel_id);
    Guild *g = ch && ch->guild_id ? store_guild(ch->guild_id) : NULL;
    if (!g)
    {
        Snowflake gid = json_id(j, "guild_id");
        g = gid ? store_guild(gid) : NULL;
    }
    m->author_color = member_color(g, member);

    Json *atts = json_get(j, "attachments");
    if (atts && atts->type == JSON_ARRAY && atts->count)
    {
        m->attachments = calloc((size_t)atts->count, sizeof(Attachment));
        for (Json *a = atts->child; a && m->attachments; a = a->next)
        {
            Attachment *at = &m->attachments[m->attachment_count++];
            at->url = xstrdup(json_str(a, "url"));
            at->proxy_url = xstrdup(json_str(a, "proxy_url"));
            at->filename = xstrdup(json_str(a, "filename") ? json_str(a, "filename") : "file");
            at->content_type = xstrdup(json_str(a, "content_type"));
            at->width = (int)json_int(a, "width", 0);
            at->height = (int)json_int(a, "height", 0);
            at->size = (long)json_int(a, "size", 0);
        }
    }
    parse_embeds(m, json_get(j, "embeds"));

    Json *ref = json_get(j, "referenced_message");
    if (ref && ref->type == JSON_OBJECT)
    {
        m->is_reply = true;
        Json *ra = json_get(ref, "author");
        m->reply_author = ra ? display_name(ra, NULL) : xstrdup("Unknown");
        const char *rc = json_str(ref, "content");
        m->reply_content = xstrdup(rc && *rc ? rc : "Click to see attachment");
    }
    else if (m->type == 19)
    {
        m->is_reply = true;
        m->reply_author = xstrdup("");
        m->reply_content = xstrdup("Original message was deleted");
    }
    return m;
}

static int find_index(Channel *ch, Snowflake id)
{
    int lo = 0, hi = ch->message_count;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (ch->messages[mid]->id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static void channel_insert(Channel *ch, Message *m)
{
    int at = find_index(ch, m->id);
    if (at < ch->message_count && ch->messages[at]->id == m->id)
    {
        message_free(ch->messages[at]);
        ch->messages[at] = m;
        return;
    }
    if (ch->message_count == ch->message_cap)
    {
        int cap = ch->message_cap ? ch->message_cap * 2 : 64;
        Message **n = realloc(ch->messages, sizeof(Message *) * (size_t)cap);
        if (!n)
        {
            message_free(m);
            return;
        }
        ch->messages = n;
        ch->message_cap = cap;
    }
    memmove(&ch->messages[at + 1], &ch->messages[at], sizeof(Message *) * (size_t)(ch->message_count - at));
    ch->messages[at] = m;
    ch->message_count++;
}

static void channel_remove_at(Channel *ch, int i)
{
    message_free(ch->messages[i]);
    memmove(&ch->messages[i], &ch->messages[i + 1], sizeof(Message *) * (size_t)(ch->message_count - i - 1));
    ch->message_count--;
}

void store_on_message_create(Json *j)
{
    Snowflake cid = json_id(j, "channel_id");
    Channel *ch = store_channel(cid);
    if (!ch)
        return;
    ch->last_message_id = json_id(j, "id");
    if (!ch->guild_id)
        qsort(S.dms, (size_t)S.dm_count, sizeof(Channel *), dm_order);
    if (!ch->loaded)
    {
        S.version++;
        return;
    }
    Message *m = message_parse(j);
    if (!m)
        return;
    if (m->nonce)
        for (int i = ch->message_count - 1; i >= 0; i--)
        {
            Message *p = ch->messages[i];
            if (p->pending && p->nonce && strcmp(p->nonce, m->nonce) == 0)
            {
                channel_remove_at(ch, i);
                break;
            }
        }
    channel_insert(ch, m);
    S.version++;
}

void store_on_message_update(Json *j)
{
    Channel *ch = store_channel(json_id(j, "channel_id"));
    if (!ch)
        return;
    Snowflake id = json_id(j, "id");
    int at = find_index(ch, id);
    if (at >= ch->message_count || ch->messages[at]->id != id)
        return;
    Message *m = ch->messages[at];
    if (json_get(j, "author") && json_str(j, "content"))
    {
        Message *fresh = message_parse(j);
        if (fresh)
        {
            fresh->author_color = fresh->author_color ? fresh->author_color : m->author_color;
            message_free(m);
            ch->messages[at] = fresh;
        }
    }
    else
    {
        const char *content = json_str(j, "content");
        if (content)
            set_str(&m->content, content);
        if (json_str(j, "edited_timestamp"))
            m->edited = true;
        Json *embeds = json_get(j, "embeds");
        if (embeds)
            parse_embeds(m, embeds);
        rich_free(m->layout);
        m->layout = NULL;
        m->layout_width = 0;
    }
    S.version++;
}

void store_on_message_delete(Json *j)
{
    Channel *ch = store_channel(json_id(j, "channel_id"));
    if (!ch)
        return;
    Snowflake id = json_id(j, "id");
    int at = find_index(ch, id);
    if (at < ch->message_count && ch->messages[at]->id == id)
    {
        channel_remove_at(ch, at);
        S.version++;
    }
}

void store_load_messages(Channel *ch, Json *arr, bool older)
{
    ch->loading = false;
    if (!arr || arr->type != JSON_ARRAY)
        return;
    for (Json *j = arr->child; j; j = j->next)
    {
        Message *m = message_parse(j);
        if (m)
            channel_insert(ch, m);
    }
    if (!older || arr->count < 50)
        ch->has_more = arr->count >= 50;
    ch->loaded = true;
    S.version++;
}

Message *store_add_pending(Channel *ch, const char *content, const char *nonce)
{
    Message *m = calloc(1, sizeof(Message));
    if (!m)
        return NULL;
    m->id = UINT64_MAX - 1000000 + ++pending_counter;
    m->channel_id = ch->id;
    m->author_id = S.me.id;
    m->author_name = xstrdup(S.me.global_name && *S.me.global_name ? S.me.global_name : S.me.username ? S.me.username : "You");
    m->author_avatar = xstrdup(S.me.avatar);
    m->author_disc = xstrdup(S.me.discriminator);
    m->content = xstrdup(content);
    m->nonce = xstrdup(nonce);
    m->timestamp = time(NULL);
    m->pending = true;
    channel_insert(ch, m);
    S.version++;
    return m;
}

void store_mark_failed(Channel *ch, const char *nonce)
{
    for (int i = 0; i < ch->message_count; i++)
    {
        Message *m = ch->messages[i];
        if (m->pending && m->nonce && strcmp(m->nonce, nonce) == 0)
        {
            m->failed = true;
            S.version++;
        }
    }
}
