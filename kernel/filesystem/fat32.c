#define FAT_ATTR_READONLY 0x01
#define FAT_ATTR_HIDDEN   0x02
#define FAT_ATTR_SYSTEM   0x04
#define FAT_ATTR_VOLUME   0x08
#define FAT_ATTR_DIR      0x10
#define FAT_ATTR_ARCHIVE  0x20
#define FAT_ATTR_LFN      0x0F

#define FAT_MASK 0x0FFFFFFFu
#define FAT_EOC  0x0FFFFFFFu

#define FAT_LFN_CHARS 13
#define FAT_ENTRY     32

typedef struct PACKED
{
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  ctime_tenth;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t size;
}
FatDirent;

typedef struct PACKED
{
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
}
FatLfn;

typedef struct
{
    BlockDevice *dev;
    uint32_t spc;
    uint32_t cluster_bytes;
    uint32_t fat_start;
    uint32_t fat_sectors;
    uint32_t fat_count;
    uint32_t data_start;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t next_free;
    uint32_t cache_sector;
    bool     cache_valid;
    bool     cache_dirty;
    uint8_t  cache[BLOCK_SECTOR_SIZE];
}
Fat32;

typedef struct
{
    Fat32   *fs;
    uint32_t cluster;
    uint32_t entry_offset;
    uint8_t  lfn_count;
    uint8_t  short_name[11];
    bool     loaded;
    uint32_t hint_index;
    uint32_t hint_cluster;
}
FatNode;

static VNodeOps fat_ops;

static int fat_cache_flush(Fat32 *fs)
{
    if (!fs->cache_dirty)
        return 0;
    for (uint32_t i = 0; i < fs->fat_count; i++)
        if (block_write(fs->dev, fs->fat_start + i * fs->fat_sectors + fs->cache_sector, 1, fs->cache) != 0)
            return -1;
    fs->cache_dirty = false;
    return 0;
}

static int fat_cache_load(Fat32 *fs, uint32_t sector)
{
    if (fs->cache_valid && fs->cache_sector == sector)
        return 0;
    if (fat_cache_flush(fs) != 0)
        return -1;
    fs->cache_valid = false;
    if (block_read(fs->dev, fs->fat_start + sector, 1, fs->cache) != 0)
        return -1;
    fs->cache_sector = sector;
    fs->cache_valid = true;
    return 0;
}

static bool fat_valid(Fat32 *fs, uint32_t c)
{
    return c >= 2 && c < fs->cluster_count + 2;
}

static uint32_t fat_get(Fat32 *fs, uint32_t c)
{
    if (fat_cache_load(fs, c / 128) != 0)
        return FAT_EOC;
    uint32_t v;
    memcpy(&v, fs->cache + (c % 128) * 4, 4);
    return v & FAT_MASK;
}

static int fat_set(Fat32 *fs, uint32_t c, uint32_t value)
{
    if (fat_cache_load(fs, c / 128) != 0)
        return -1;
    uint32_t v;
    memcpy(&v, fs->cache + (c % 128) * 4, 4);
    v = (v & ~FAT_MASK) | (value & FAT_MASK);
    memcpy(fs->cache + (c % 128) * 4, &v, 4);
    fs->cache_dirty = true;
    return 0;
}

static uint64_t fat_lba(Fat32 *fs, uint32_t c)
{
    return fs->data_start + (uint64_t)(c - 2) * fs->spc;
}

static int fat_zero_cluster(Fat32 *fs, uint32_t c)
{
    uint8_t *zero = kcalloc(1, fs->cluster_bytes);
    if (!zero)
        return -1;
    int rc = block_write(fs->dev, fat_lba(fs, c), fs->spc, zero);
    kfree(zero);
    return rc;
}

static uint32_t fat_alloc(Fat32 *fs, uint32_t prev, bool zero)
{
    for (uint32_t n = 0; n < fs->cluster_count; n++)
    {
        uint32_t c = 2 + (fs->next_free - 2 + n) % fs->cluster_count;
        if (fat_get(fs, c) != 0)
            continue;
        if (zero && fat_zero_cluster(fs, c) != 0)
            return 0;
        if (fat_set(fs, c, FAT_EOC) != 0)
            return 0;
        if (prev && fat_set(fs, prev, c) != 0)
            return 0;
        fs->next_free = c + 1 < fs->cluster_count + 2 ? c + 1 : 2;
        return c;
    }
    return 0;
}

static void fat_free_chain(Fat32 *fs, uint32_t c)
{
    while (fat_valid(fs, c))
    {
        uint32_t next = fat_get(fs, c);
        fat_set(fs, c, 0);
        if (c < fs->next_free)
            fs->next_free = c;
        c = next;
    }
}

static uint32_t fat_cluster_at(FatNode *fn, uint32_t index)
{
    Fat32 *fs = fn->fs;
    uint32_t c = fn->cluster, i = 0;
    if (fn->hint_cluster && fn->hint_index <= index)
    {
        c = fn->hint_cluster;
        i = fn->hint_index;
    }
    while (i < index && fat_valid(fs, c))
    {
        c = fat_get(fs, c);
        i++;
    }
    if (!fat_valid(fs, c))
        return 0;
    fn->hint_index = index;
    fn->hint_cluster = c;
    return c;
}

static int fat_extend(FatNode *fn, uint32_t clusters, bool zero)
{
    Fat32 *fs = fn->fs;
    if (clusters == 0)
        return 0;

    if (!fat_valid(fs, fn->cluster))
    {
        uint32_t c = fat_alloc(fs, 0, zero);
        if (!c)
            return -1;
        fn->cluster = c;
        fn->hint_index = 0;
        fn->hint_cluster = c;
    }

    uint32_t i = 0, c = fn->cluster;
    if (fn->hint_cluster && fn->hint_index < clusters)
    {
        i = fn->hint_index;
        c = fn->hint_cluster;
    }
    while (i + 1 < clusters)
    {
        uint32_t next = fat_get(fs, c);
        if (!fat_valid(fs, next))
        {
            next = fat_alloc(fs, c, zero);
            if (!next)
                return -1;
        }
        c = next;
        i++;
    }
    fn->hint_index = i;
    fn->hint_cluster = c;
    return 0;
}

static int fat_data_io(Fat32 *fs, uint64_t lba, size_t off, uint8_t *buf, size_t len, bool write)
{
    while (len)
    {
        uint64_t s = lba + off / BLOCK_SECTOR_SIZE;
        size_t so = off % BLOCK_SECTOR_SIZE;
        if (so == 0 && len >= BLOCK_SECTOR_SIZE)
        {
            uint32_t n = (uint32_t)(len / BLOCK_SECTOR_SIZE);
            int rc = write ? block_write(fs->dev, s, n, buf) : block_read(fs->dev, s, n, buf);
            if (rc != 0)
                return -1;
            size_t bytes = (size_t)n * BLOCK_SECTOR_SIZE;
            off += bytes;
            buf += bytes;
            len -= bytes;
            continue;
        }

        size_t n = MIN(BLOCK_SECTOR_SIZE - so, len);
        uint8_t tmp[BLOCK_SECTOR_SIZE];
        if (block_read(fs->dev, s, 1, tmp) != 0)
            return -1;
        if (write)
        {
            memcpy(tmp + so, buf, n);
            if (block_write(fs->dev, s, 1, tmp) != 0)
                return -1;
        }
        else
        {
            memcpy(buf, tmp + so, n);
        }
        off += n;
        buf += n;
        len -= n;
    }
    return 0;
}

static ssize_t fat_io(FatNode *fn, size_t offset, void *data, size_t len, bool write)
{
    Fat32 *fs = fn->fs;
    uint8_t *buf = data;
    size_t cb = fs->cluster_bytes;
    size_t done = 0;

    while (done < len)
    {
        uint32_t index = (uint32_t)((offset + done) / cb);
        size_t within = (offset + done) % cb;
        uint32_t c = fat_cluster_at(fn, index);
        if (!c)
            break;

        size_t run = cb - within;
        uint32_t last = c, last_index = index;
        while (run < len - done)
        {
            uint32_t next = fat_get(fs, last);
            if (next != last + 1 || !fat_valid(fs, next))
                break;
            last = next;
            last_index++;
            run += cb;
        }
        run = MIN(run, len - done);
        fn->hint_index = last_index;
        fn->hint_cluster = last;

        if (fat_data_io(fs, fat_lba(fs, c), within, buf + done, run, write) != 0)
            return done ? (ssize_t)done : -1;
        done += run;
    }
    return (ssize_t)done;
}

static void fat_timestamp(FatDirent *e, bool created)
{
    uint16_t time = 0, date = (1 << 5) | 1;
    RtcTime t;
    if (rtc_read(&t) == 0 && t.year >= 1980)
    {
        time = (uint16_t)((t.hour << 11) | (t.minute << 5) | (t.second / 2));
        date = (uint16_t)(((t.year - 1980) << 9) | (t.month << 5) | t.day);
    }
    e->mtime = time;
    e->mdate = date;
    e->adate = date;
    if (created)
    {
        e->ctime = time;
        e->cdate = date;
    }
}

static uint8_t fat_checksum(const uint8_t *name)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name[i]);
    return sum;
}

static char fat_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static char fat_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool fat_name_eq(const char *a, const char *b)
{
    while (*a && *b)
        if (fat_upper(*a++) != fat_upper(*b++))
            return false;
    return *a == *b;
}

static VNode *fat_new_vnode(VNode *dir, const char *name, VNodeType type, size_t size, FatNode *fn)
{
    VNode *n = kcalloc(1, sizeof(VNode));
    if (!n)
        return NULL;

    size_t len = MIN(strlen(name), (size_t)VFS_NAME_MAX - 1);
    memcpy(n->name, name, len);
    n->name[len] = '\0';
    n->type = type;
    n->size = size;
    n->ops = &fat_ops;
    n->private_data = fn;
    n->parent = dir;

    if (dir)
    {
        VNode **pp = &dir->first_child;
        while (*pp)
            pp = &(*pp)->next_sibling;
        *pp = n;
    }
    return n;
}

static void fat_short_to_name(const FatDirent *e, char *out)
{
    int n = 0;
    int base = 8;
    while (base > 0 && e->name[base - 1] == ' ')
        base--;
    for (int i = 0; i < base; i++)
    {
        char c = (char)(i == 0 && e->name[0] == 0x05 ? 0xE5 : e->name[i]);
        out[n++] = (e->ntres & 0x08) ? fat_lower(c) : c;
    }
    int ext = 3;
    while (ext > 0 && e->name[8 + ext - 1] == ' ')
        ext--;
    if (ext)
    {
        out[n++] = '.';
        for (int i = 0; i < ext; i++)
            out[n++] = (e->ntres & 0x10) ? fat_lower((char)e->name[8 + i]) : (char)e->name[8 + i];
    }
    out[n] = '\0';
}

static void fat_load_dir(VNode *dir)
{
    FatNode *dn = dir->private_data;
    if (dn->loaded)
        return;
    dn->loaded = true;

    Fat32 *fs = dn->fs;
    uint8_t *buf = kmalloc(fs->cluster_bytes);
    if (!buf)
        return;

    uint16_t lfn[20 * FAT_LFN_CHARS + 1];
    int lfn_count = 0;
    uint8_t lfn_sum = 0;

    for (uint32_t index = 0;; index++)
    {
        uint32_t c = fat_cluster_at(dn, index);
        if (!c || block_read(fs->dev, fat_lba(fs, c), fs->spc, buf) != 0)
            break;

        for (uint32_t off = 0; off < fs->cluster_bytes; off += FAT_ENTRY)
        {
            FatDirent *e = (FatDirent *)(buf + off);
            if (e->name[0] == 0x00)
                goto done;
            if (e->name[0] == 0xE5)
            {
                lfn_count = 0;
                continue;
            }

            if (e->attr == FAT_ATTR_LFN)
            {
                FatLfn *l = (FatLfn *)e;
                int seq = l->order & 0x1F;
                if (seq < 1 || seq > 20)
                {
                    lfn_count = 0;
                    continue;
                }
                if (l->order & 0x40)
                {
                    lfn_count = seq;
                    lfn_sum = l->checksum;
                    for (int i = 0; i <= seq * FAT_LFN_CHARS; i++)
                        lfn[i] = 0;
                }
                uint16_t *dst = lfn + (seq - 1) * FAT_LFN_CHARS;
                memcpy(dst, l->name1, 10);
                memcpy(dst + 5, l->name2, 12);
                memcpy(dst + 11, l->name3, 4);
                continue;
            }

            if ((e->attr & FAT_ATTR_VOLUME) || e->name[0] == '.')
            {
                lfn_count = 0;
                continue;
            }

            char name[VFS_NAME_MAX];
            uint8_t used_lfn = 0;
            if (lfn_count && lfn_sum == fat_checksum(e->name))
            {
                int n = 0;
                for (int i = 0; i < lfn_count * FAT_LFN_CHARS && n < VFS_NAME_MAX - 1; i++)
                {
                    uint16_t ch = lfn[i];
                    if (ch == 0 || ch == 0xFFFF)
                        break;
                    name[n++] = ch < 0x80 ? (char)ch : '_';
                }
                name[n] = '\0';
                used_lfn = (uint8_t)lfn_count;
            }
            else
            {
                fat_short_to_name(e, name);
            }
            lfn_count = 0;

            FatNode *fn = kcalloc(1, sizeof(FatNode));
            if (!fn)
                goto done;
            fn->fs = fs;
            fn->cluster = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
            fn->entry_offset = index * fs->cluster_bytes + off;
            fn->lfn_count = used_lfn;
            memcpy(fn->short_name, e->name, 11);

            bool is_dir = (e->attr & FAT_ATTR_DIR) != 0;
            if (!fat_new_vnode(dir, name, is_dir ? VNODE_DIR : VNODE_FILE, is_dir ? 0 : e->size, fn))
            {
                kfree(fn);
                goto done;
            }
        }
    }

done:
    kfree(buf);
}

static VNode *fat_lookup(VNode *dir, const char *name)
{
    if (dir->type != VNODE_DIR)
        return NULL;
    fat_load_dir(dir);
    for (VNode *c = dir->first_child; c; c = c->next_sibling)
        if (fat_name_eq(c->name, name))
            return c;
    return NULL;
}

static int fat_readdir(VNode *dir, int i, char *name_out)
{
    fat_load_dir(dir);
    int idx = 0;
    for (VNode *c = dir->first_child; c; c = c->next_sibling, idx++)
    {
        if (idx == i)
        {
            strcpy(name_out, c->name);
            return 0;
        }
    }
    return -1;
}

static bool fat_short_taken(VNode *dir, const uint8_t *name)
{
    for (VNode *c = dir->first_child; c; c = c->next_sibling)
        if (memcmp(((FatNode *)c->private_data)->short_name, name, 11) == 0)
            return true;
    return false;
}

static bool fat_short_char(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    return c && strchr("$%'-_@~`!(){}^#&", c) != NULL;
}

static int fat_make_short(VNode *dir, const char *name, uint8_t *out)
{
    memset(out, ' ', 11);
    const char *dot = strrchr(name, '.');
    if (dot == name)
        dot = NULL;

    int n = 0;
    for (const char *p = name; *p && p != dot && n < 6; p++)
        if (fat_short_char(fat_upper(*p)))
            out[n++] = (uint8_t)fat_upper(*p);
    if (n == 0)
        out[n++] = '_';

    if (dot)
    {
        int k = 0;
        for (const char *p = dot + 1; *p && k < 3; p++)
            if (fat_short_char(fat_upper(*p)))
                out[8 + k++] = (uint8_t)fat_upper(*p);
    }

    for (uint32_t num = 1; num < 1000000; num++)
    {
        char tail[8];
        int t = 0;
        char digits[8];
        int d = 0;
        for (uint32_t v = num; v; v /= 10)
            digits[d++] = (char)('0' + v % 10);
        tail[t++] = '~';
        while (d)
            tail[t++] = digits[--d];

        int pos = MIN(n, 8 - t);
        for (int i = 0; i < 8; i++)
            if (i >= pos)
                out[i] = ' ';
        memcpy(out + pos, tail, (size_t)t);
        if (!fat_short_taken(dir, out))
            return 0;
    }
    return -1;
}

static int64_t fat_find_slots(FatNode *dn, uint32_t needed)
{
    Fat32 *fs = dn->fs;
    uint8_t *buf = kmalloc(fs->cluster_bytes);
    if (!buf)
        return -1;

    uint32_t run = 0;
    uint64_t run_start = 0;
    uint32_t index;
    for (index = 0;; index++)
    {
        uint32_t c = fat_cluster_at(dn, index);
        if (!c)
            break;
        if (block_read(fs->dev, fat_lba(fs, c), fs->spc, buf) != 0)
        {
            kfree(buf);
            return -1;
        }
        for (uint32_t off = 0; off < fs->cluster_bytes; off += FAT_ENTRY)
        {
            uint64_t pos = (uint64_t)index * fs->cluster_bytes + off;
            if (buf[off] == 0x00)
            {
                if (!run)
                    run_start = pos;
                goto extend;
            }
            if (buf[off] == 0xE5)
            {
                if (!run++)
                    run_start = pos;
                if (run == needed)
                {
                    kfree(buf);
                    return (int64_t)run_start;
                }
            }
            else
            {
                run = 0;
            }
        }
    }
    if (!run)
        run_start = (uint64_t)index * fs->cluster_bytes;

extend:
    kfree(buf);
    if (fat_extend(dn, (uint32_t)DIV_CEIL(run_start + needed * FAT_ENTRY, fs->cluster_bytes), true) != 0)
        return -1;
    return (int64_t)run_start;
}

static int fat_add_entry(VNode *dir, const char *name, uint8_t attr, uint32_t cluster, uint32_t size, FatNode *fn)
{
    FatNode *dn = dir->private_data;
    size_t len = strlen(name);
    if (len == 0 || len >= VFS_NAME_MAX)
        return -1;

    uint8_t short_name[11];
    if (fat_make_short(dir, name, short_name) != 0)
        return -1;

    uint32_t lfn_count = (uint32_t)DIV_CEIL(len, FAT_LFN_CHARS);
    uint32_t total = lfn_count + 1;
    int64_t pos = fat_find_slots(dn, total);
    if (pos < 0)
        return -1;

    uint8_t entries[(VFS_NAME_MAX / FAT_LFN_CHARS + 2) * FAT_ENTRY];
    memset(entries, 0, sizeof(entries));
    uint8_t sum = fat_checksum(short_name);

    for (uint32_t i = 0; i < lfn_count; i++)
    {
        uint32_t seq = lfn_count - i;
        FatLfn *l = (FatLfn *)(entries + i * FAT_ENTRY);
        l->order = (uint8_t)(seq | (i == 0 ? 0x40 : 0));
        l->attr = FAT_ATTR_LFN;
        l->checksum = sum;

        uint16_t chars[FAT_LFN_CHARS];
        for (uint32_t k = 0; k < FAT_LFN_CHARS; k++)
        {
            size_t at = (seq - 1) * FAT_LFN_CHARS + k;
            chars[k] = at < len ? (uint8_t)name[at] : (at == len ? 0x0000 : 0xFFFF);
        }
        memcpy(l->name1, chars, 10);
        memcpy(l->name2, chars + 5, 12);
        memcpy(l->name3, chars + 11, 4);
    }

    FatDirent *e = (FatDirent *)(entries + lfn_count * FAT_ENTRY);
    memcpy(e->name, short_name, 11);
    e->attr = attr;
    fat_timestamp(e, true);
    e->cluster_hi = (uint16_t)(cluster >> 16);
    e->cluster_lo = (uint16_t)cluster;
    e->size = size;

    if (fat_io(dn, (size_t)pos, entries, total * FAT_ENTRY, true) != (ssize_t)(total * FAT_ENTRY))
        return -1;

    fn->entry_offset = (uint32_t)pos + lfn_count * FAT_ENTRY;
    fn->lfn_count = (uint8_t)lfn_count;
    memcpy(fn->short_name, short_name, 11);
    return 0;
}

static int fat_remove_entry(VNode *dir, FatNode *fn)
{
    FatNode *dn = dir->private_data;
    uint8_t mark = 0xE5;
    for (int k = 0; k <= fn->lfn_count; k++)
        if (fat_io(dn, fn->entry_offset - (uint32_t)k * FAT_ENTRY, &mark, 1, true) != 1)
            return -1;
    return 0;
}

static int fat_update_entry(VNode *node)
{
    if (!node->parent)
        return 0;
    FatNode *fn = node->private_data;
    FatNode *dn = node->parent->private_data;

    FatDirent e;
    if (fat_io(dn, fn->entry_offset, &e, sizeof(e), false) != sizeof(e))
        return -1;
    e.cluster_hi = (uint16_t)(fn->cluster >> 16);
    e.cluster_lo = (uint16_t)fn->cluster;
    e.size = node->type == VNODE_DIR ? 0 : (uint32_t)node->size;
    fat_timestamp(&e, false);
    return fat_io(dn, fn->entry_offset, &e, sizeof(e), true) == sizeof(e) ? 0 : -1;
}

static void fat_dot_entry(FatDirent *e, const char *name, uint32_t cluster)
{
    memset(e, 0, sizeof(*e));
    memset(e->name, ' ', 11);
    memcpy(e->name, name, strlen(name));
    e->attr = FAT_ATTR_DIR;
    fat_timestamp(e, true);
    e->cluster_hi = (uint16_t)(cluster >> 16);
    e->cluster_lo = (uint16_t)cluster;
}

static uint32_t fat_dir_cluster(VNode *dir)
{
    FatNode *dn = dir->private_data;
    return dir->parent ? dn->cluster : 0;
}

static VNode *fat_create(VNode *dir, const char *name, VNodeType type)
{
    if (type != VNODE_FILE && type != VNODE_DIR)
        return NULL;
    if (fat_lookup(dir, name))
        return NULL;

    FatNode *dn = dir->private_data;
    Fat32 *fs = dn->fs;
    FatNode *fn = kcalloc(1, sizeof(FatNode));
    if (!fn)
        return NULL;
    fn->fs = fs;
    fn->loaded = true;

    if (type == VNODE_DIR)
    {
        fn->cluster = fat_alloc(fs, 0, true);
        if (!fn->cluster)
            goto fail;
        FatDirent dots[2];
        fat_dot_entry(&dots[0], ".", fn->cluster);
        fat_dot_entry(&dots[1], "..", fat_dir_cluster(dir));
        if (fat_io(fn, 0, dots, sizeof(dots), true) != sizeof(dots))
            goto fail;
    }

    if (fat_add_entry(dir, name, type == VNODE_DIR ? FAT_ATTR_DIR : FAT_ATTR_ARCHIVE, fn->cluster, 0, fn) != 0)
        goto fail;

    VNode *n = fat_new_vnode(dir, name, type, 0, fn);
    if (!n)
        goto fail;
    fat_cache_flush(fs);
    return n;

fail:
    fat_free_chain(fs, fn->cluster);
    fat_cache_flush(fs);
    kfree(fn);
    return NULL;
}

static ssize_t fat_read(VNode *node, void *buf, size_t count, size_t offset)
{
    if (node->type != VNODE_FILE || offset >= node->size)
        return 0;
    count = MIN(count, node->size - offset);
    return fat_io(node->private_data, offset, buf, count, false);
}

static ssize_t fat_write(VNode *node, const void *buf, size_t count, size_t offset)
{
    if (node->type != VNODE_FILE)
        return -1;
    if (count == 0)
        return 0;

    FatNode *fn = node->private_data;
    Fat32 *fs = fn->fs;
    uint64_t end = (uint64_t)offset + count;
    if (end > 0xFFFFFFFFULL)
        return -1;

    uint32_t old_cluster = fn->cluster;
    uint32_t hint_index = fn->hint_index, hint_cluster = fn->hint_cluster;
    if (fat_extend(fn, (uint32_t)DIV_CEIL(end, fs->cluster_bytes), false) != 0)
    {
        fat_cache_flush(fs);
        return -1;
    }
    if (hint_cluster && fn->cluster == old_cluster && hint_index <= offset / fs->cluster_bytes)
    {
        fn->hint_index = hint_index;
        fn->hint_cluster = hint_cluster;
    }

    ssize_t n = fat_io(fn, offset, (void *)buf, count, true);
    if (n > 0 && offset + (size_t)n > node->size)
        node->size = offset + (size_t)n;
    if (n > 0 || fn->cluster != old_cluster)
        fat_update_entry(node);
    fat_cache_flush(fs);
    return n;
}

static void fat_detach(VNode *node)
{
    VNode **pp = &node->parent->first_child;
    while (*pp && *pp != node)
        pp = &(*pp)->next_sibling;
    if (*pp)
        *pp = node->next_sibling;
    node->next_sibling = NULL;
}

static void fat_release_node(VNode *node)
{
    FatNode *fn = node->private_data;
    fat_free_chain(fn->fs, fn->cluster);
    fat_cache_flush(fn->fs);
    kfree(fn);
    kfree(node);
}

static int fat_unlink(VNode *dir, const char *name)
{
    VNode *node = fat_lookup(dir, name);
    if (!node)
        return -1;
    if (node->type == VNODE_DIR)
    {
        fat_load_dir(node);
        if (node->first_child)
            return -1;
    }

    FatNode *fn = node->private_data;
    if (fat_remove_entry(dir, fn) != 0)
        return -1;

    fat_detach(node);
    node->parent = NULL;
    if (node->open_count)
    {
        node->unlinked = true;
        fat_cache_flush(fn->fs);
    }
    else
    {
        fat_release_node(node);
    }
    return 0;
}

static int fat_rename(VNode *node, VNode *new_dir, const char *new_name)
{
    FatNode *fn = node->private_data;
    FatNode *dn = new_dir->private_data;
    if (!node->parent || fn->fs != dn->fs || new_dir->type != VNODE_DIR)
        return -1;
    fat_load_dir(new_dir);

    FatNode old = *fn;
    uint8_t attr = node->type == VNODE_DIR ? FAT_ATTR_DIR : FAT_ATTR_ARCHIVE;
    if (fat_add_entry(new_dir, new_name, attr, fn->cluster, (uint32_t)node->size, fn) != 0)
        return -1;
    fat_remove_entry(node->parent, &old);

    VNode *old_parent = node->parent;
    fat_detach(node);
    node->parent = new_dir;
    VNode **pp = &new_dir->first_child;
    while (*pp)
        pp = &(*pp)->next_sibling;
    *pp = node;

    size_t len = MIN(strlen(new_name), (size_t)VFS_NAME_MAX - 1);
    memcpy(node->name, new_name, len);
    node->name[len] = '\0';

    if (node->type == VNODE_DIR && old_parent != new_dir)
    {
        FatDirent dotdot;
        fat_dot_entry(&dotdot, "..", fat_dir_cluster(new_dir));
        fat_io(fn, FAT_ENTRY, &dotdot, sizeof(dotdot), true);
    }

    fat_cache_flush(fn->fs);
    return 0;
}

static bool fat_parse_boot(BlockDevice *dev, uint8_t *bs, Fat32 *fs)
{
    if (block_read(dev, 0, 1, bs) != 0 || bs[510] != 0x55 || bs[511] != 0xAA)
        return false;

    uint16_t bps, reserved, root_entries, fat16, total16;
    uint32_t total32, fat32, root;
    memcpy(&bps, bs + 11, 2);
    memcpy(&reserved, bs + 14, 2);
    memcpy(&root_entries, bs + 17, 2);
    memcpy(&total16, bs + 19, 2);
    memcpy(&fat16, bs + 22, 2);
    memcpy(&total32, bs + 32, 4);
    memcpy(&fat32, bs + 36, 4);
    memcpy(&root, bs + 44, 4);

    uint8_t spc = bs[13], fats = bs[16];
    if (bps != BLOCK_SECTOR_SIZE || spc == 0 || (spc & (spc - 1)) || fats == 0 ||
        root_entries != 0 || fat16 != 0 || fat32 == 0)
        return false;

    uint32_t total = total16 ? total16 : total32;
    uint32_t data_start = reserved + fats * fat32;
    if (total <= data_start || total > dev->sectors)
        return false;

    fs->dev = dev;
    fs->spc = spc;
    fs->cluster_bytes = spc * BLOCK_SECTOR_SIZE;
    fs->fat_start = reserved;
    fs->fat_sectors = fat32;
    fs->fat_count = fats;
    fs->data_start = data_start;
    fs->root_cluster = root;
    fs->cluster_count = MIN((total - data_start) / spc, fat32 * 128 - 2);
    fs->next_free = 2;
    return fat_valid(fs, root);
}

bool fat32_has_label(BlockDevice *dev, const char *label)
{
    uint8_t bs[BLOCK_SECTOR_SIZE];
    Fat32 fs;
    if (!fat_parse_boot(dev, bs, &fs))
        return false;
    char want[11];
    memset(want, ' ', 11);
    memcpy(want, label, MIN(strlen(label), (size_t)11));
    return memcmp(bs + 71, want, 11) == 0;
}

VNode *fat32_mount(BlockDevice *dev)
{
    fat_ops.lookup  = fat_lookup;
    fat_ops.create  = fat_create;
    fat_ops.read    = fat_read;
    fat_ops.write   = fat_write;
    fat_ops.readdir = fat_readdir;
    fat_ops.unlink  = fat_unlink;
    fat_ops.rename  = fat_rename;
    fat_ops.release = fat_release_node;

    Fat32 *fs = kcalloc(1, sizeof(Fat32));
    FatNode *fn = kcalloc(1, sizeof(FatNode));
    uint8_t bs[BLOCK_SECTOR_SIZE];
    if (!fs || !fn || !fat_parse_boot(dev, bs, fs))
    {
        kfree(fs);
        kfree(fn);
        return NULL;
    }

    fn->fs = fs;
    fn->cluster = fs->root_cluster;
    VNode *root = fat_new_vnode(NULL, "/", VNODE_DIR, 0, fn);
    if (!root)
    {
        kfree(fs);
        kfree(fn);
        return NULL;
    }

    dev->mounts++;
    kprintf("fat32: mounted %s (%u clusters of %u bytes)\n", dev->name, fs->cluster_count, fs->cluster_bytes);
    return root;
}

bool fat32_is_root(VNode *node)
{
    return node && node->ops == &fat_ops && !node->parent && !node->unlinked;
}

bool fat32_owns(VNode *root, VNode *node)
{
    if (!fat32_is_root(root) || !node || node->ops != &fat_ops)
        return false;
    return ((FatNode *)node->private_data)->fs == ((FatNode *)root->private_data)->fs;
}

static void fat_release(VNode *node)
{
    VNode *c = node->first_child;
    while (c)
    {
        VNode *next = c->next_sibling;
        fat_release(c);
        c = next;
    }
    kfree(node->private_data);
    kfree(node);
}

int fat32_unmount(VNode *root)
{
    if (!fat32_is_root(root))
        return -1;
    FatNode *fn = root->private_data;
    Fat32 *fs = fn->fs;
    fat_cache_flush(fs);
    block_flush(fs->dev);
    fs->dev->mounts--;
    fat_release(root);
    kfree(fs);
    return 0;
}

static int fat_zero_range(BlockDevice *dev, uint64_t lba, uint64_t count, uint8_t *zero)
{
    while (count)
    {
        uint32_t n = (uint32_t)MIN(count, (uint64_t)BLOCK_MAX_TRANSFER);
        if (block_write(dev, lba, n, zero) != 0)
            return -1;
        lba += n;
        count -= n;
    }
    return 0;
}

int fat32_format(BlockDevice *dev, const char *label)
{
    uint64_t total = MIN(dev->sectors, 0xFFFFFFFFULL);
    uint32_t spc = total <= 532480 ? 1 : total <= 16777216 ? 8 : total <= 33554432 ? 16 : total <= 67108864 ? 32 : 64;
    uint32_t reserved = 32, fats = 2;
    uint32_t tmp = (256 * spc + fats) / 2;
    uint32_t fat_sectors = (uint32_t)DIV_CEIL(total - reserved, tmp);
    uint32_t data_start = reserved + fats * fat_sectors;
    if (total <= data_start || (total - data_start) / spc < 65525)
    {
        kprintf("fat32: %s is too small for FAT32\n", dev->name);
        return -1;
    }

    uint8_t *zero = kcalloc(BLOCK_MAX_TRANSFER, BLOCK_SECTOR_SIZE);
    if (!zero)
        return -1;

    int rc = -1;
    if (fat_zero_range(dev, 0, data_start, zero) != 0 || fat_zero_range(dev, data_start, spc, zero) != 0)
        goto out;

    uint8_t bs[BLOCK_SECTOR_SIZE];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB;
    bs[1] = 0x58;
    bs[2] = 0x90;
    memcpy(bs + 3, "MSWIN4.1", 8);
    uint16_t v16 = BLOCK_SECTOR_SIZE;
    memcpy(bs + 11, &v16, 2);
    bs[13] = (uint8_t)spc;
    v16 = (uint16_t)reserved;
    memcpy(bs + 14, &v16, 2);
    bs[16] = (uint8_t)fats;
    bs[21] = 0xF8;
    v16 = 63;
    memcpy(bs + 24, &v16, 2);
    v16 = 255;
    memcpy(bs + 26, &v16, 2);
    uint32_t v32 = (uint32_t)dev->offset;
    memcpy(bs + 28, &v32, 4);
    v32 = (uint32_t)total;
    memcpy(bs + 32, &v32, 4);
    memcpy(bs + 36, &fat_sectors, 4);
    v32 = 2;
    memcpy(bs + 44, &v32, 4);
    v16 = 1;
    memcpy(bs + 48, &v16, 2);
    v16 = 6;
    memcpy(bs + 50, &v16, 2);
    bs[64] = 0x80;
    bs[66] = 0x29;
    v32 = (uint32_t)block_random();
    memcpy(bs + 67, &v32, 4);
    memset(bs + 71, ' ', 11);
    memcpy(bs + 71, label, MIN(strlen(label), (size_t)11));
    memcpy(bs + 82, "FAT32   ", 8);
    bs[510] = 0x55;
    bs[511] = 0xAA;

    uint8_t info[BLOCK_SECTOR_SIZE];
    memset(info, 0, sizeof(info));
    v32 = 0x41615252;
    memcpy(info, &v32, 4);
    v32 = 0x61417272;
    memcpy(info + 484, &v32, 4);
    v32 = 0xFFFFFFFF;
    memcpy(info + 488, &v32, 4);
    memcpy(info + 492, &v32, 4);
    v32 = 0xAA550000;
    memcpy(info + 508, &v32, 4);

    if (block_write(dev, 0, 1, bs) != 0 || block_write(dev, 1, 1, info) != 0 ||
        block_write(dev, 6, 1, bs) != 0 || block_write(dev, 7, 1, info) != 0)
        goto out;

    uint8_t fat[BLOCK_SECTOR_SIZE];
    memset(fat, 0, sizeof(fat));
    uint32_t first[3] = { 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF };
    memcpy(fat, first, sizeof(first));
    for (uint32_t i = 0; i < fats; i++)
        if (block_write(dev, reserved + i * fat_sectors, 1, fat) != 0)
            goto out;

    uint8_t root[BLOCK_SECTOR_SIZE];
    memset(root, 0, sizeof(root));
    FatDirent *vol = (FatDirent *)root;
    memcpy(vol->name, bs + 71, 11);
    vol->attr = FAT_ATTR_VOLUME;
    fat_timestamp(vol, true);
    if (block_write(dev, data_start, 1, root) != 0)
        goto out;

    rc = block_flush(dev);
    kprintf("fat32: formatted %s\n", dev->name);

out:
    kfree(zero);
    return rc;
}
