#include <stdlib.h>
#include <memory.h>

#define ALIGN        16u
#define HDR_SIZE     sizeof(BlockHdr)

/* Small size classes: payload 16, 32, 48 … 512 bytes. */
#define SMALL_GRAN   16u
#define MAX_SMALL    512u
#define NUM_CLASSES  (MAX_SMALL / SMALL_GRAN)  /* 32 */

/* Minimum heap growth. */
#define GROW_MIN     (4096u * 4u)   /* 16 KiB */

#define BLOCK_FREE      1u
#define BLOCK_SENTINEL  2u

/* ── block header (must be exactly ALIGN bytes) ────────────────────────── */

typedef struct BlockHdr {
    size_t           size;       /* total bytes incl. header; 0 = sentinel  */
    unsigned         flags;      /* BLOCK_FREE | BLOCK_SENTINEL              */
    struct BlockHdr *next_free;  /* next on free list (only when free)       */
    unsigned         _pad;       /* pad struct to 16 bytes                   */
} BlockHdr;

/*
 * For large free blocks we stash prev_free in the first 8 bytes of the
 * user region, giving us O(1) removal from the doubly-linked large list.
 */
#define LARGE_PREV(h) (*(BlockHdr **)((char *)(h) + HDR_SIZE))

static int       g_ready;
static char     *g_heap_start;
static char     *g_heap_end;         /* points at the sentinel BlockHdr */
static BlockHdr *g_small[NUM_CLASSES];
static BlockHdr *g_large;

/* ── size utilities ────────────────────────────────────────────────────── */

static size_t au(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }

/* Total block bytes needed for `sz` bytes of user payload. */
static size_t total_for(size_t sz)
{
    size_t t = au(HDR_SIZE + sz, ALIGN);
    return t < HDR_SIZE + ALIGN ? HDR_SIZE + ALIGN : t;
}

static BlockHdr *block_next(BlockHdr *h) { return (BlockHdr *)((char *)h + h->size); }

static void sentinel_write(char *at)
{
    BlockHdr *s = (BlockHdr *)at;
    s->size = 0; s->flags = BLOCK_SENTINEL; s->next_free = 0; s->_pad = 0;
}

/* ── free-list helpers ─────────────────────────────────────────────────── */

/* Returns size-class index for payload `p`, or -1 if large. */
static int sclass(size_t p)
{
    if (p == 0) p = 1;
    size_t c = (p + SMALL_GRAN - 1) / SMALL_GRAN;
    return (c <= NUM_CLASSES) ? (int)(c - 1) : -1;
}

static void fl_push_small(BlockHdr *h, int c)
{
    h->flags = BLOCK_FREE; h->next_free = g_small[c]; g_small[c] = h;
}
static BlockHdr *fl_pop_small(int c)
{
    BlockHdr *h = g_small[c]; if (h) g_small[c] = h->next_free; return h;
}

static void fl_push_large(BlockHdr *h)
{
    h->flags = BLOCK_FREE; h->next_free = g_large; LARGE_PREV(h) = 0;
    if (g_large) LARGE_PREV(g_large) = h;
    g_large = h;
}
static void fl_remove_large(BlockHdr *h)
{
    BlockHdr *prv = LARGE_PREV(h), *nxt = h->next_free;
    if (prv) prv->next_free = nxt; else g_large = nxt;
    if (nxt) LARGE_PREV(nxt) = prv;
}

/* Best-fit search through the large list. */
static BlockHdr *fl_find_large(size_t total)
{
    BlockHdr *best = 0;
    for (BlockHdr *b = g_large; b; b = b->next_free)
        if (b->size >= total && (!best || b->size < best->size))
            best = b;
    return best;
}

/* Remove a block from whichever free list it lives on. */
static void fl_remove(BlockHdr *h)
{
    int c = sclass(h->size - HDR_SIZE);
    if (c >= 0) {
        BlockHdr **pp = &g_small[c];
        while (*pp && *pp != h) pp = &(*pp)->next_free;
        if (*pp) *pp = h->next_free;
    } else {
        fl_remove_large(h);
    }
}

/* ── heap init ─────────────────────────────────────────────────────────── */

static int heap_init(void)
{
    void *brk = segbrk(0);
    if (brk == (void *)-1) return -1;

    void *new_brk = (char *)brk + GROW_MIN + HDR_SIZE;
    if (segbrk(new_brk) == (void *)-1) return -1;

    g_heap_start = (char *)brk;
    g_heap_end   = (char *)new_brk - HDR_SIZE;

    BlockHdr *blk = (BlockHdr *)g_heap_start;
    blk->size = (size_t)(g_heap_end - g_heap_start);
    blk->flags = 0; blk->next_free = 0; blk->_pad = 0;
    fl_push_large(blk);
    sentinel_write(g_heap_end);

    g_ready = 1;
    return 0;
}

/* ── grow the heap, returning a new free block ─────────────────────────── */

static BlockHdr *heap_grow(size_t need)
{
    size_t grow = au(need < GROW_MIN ? GROW_MIN : need, ALIGN);
    char  *old_end = g_heap_end;
    char  *new_end = old_end + grow + HDR_SIZE;

    if (segbrk(new_end) == (void *)-1) return 0;

    BlockHdr *blk = (BlockHdr *)old_end;
    blk->size = (size_t)(new_end - HDR_SIZE - old_end);
    blk->flags = 0; blk->next_free = 0; blk->_pad = 0;

    g_heap_end = new_end - HDR_SIZE;
    sentinel_write(g_heap_end);

    /* Forward-coalesce with the last block in the heap if it's free. */
    BlockHdr *prev = 0;
    for (BlockHdr *b = (BlockHdr *)g_heap_start;
         b != (BlockHdr *)old_end; b = block_next(b))
        prev = b;

    if (prev && (prev->flags & BLOCK_FREE)) {
        fl_remove(prev);
        prev->size += blk->size;
        prev->flags = 0;
        fl_push_large(prev);
        return prev;
    }

    fl_push_large(blk);
    return blk;
}

/* ── split a block if the surplus is usable ────────────────────────────── */

static void maybe_split(BlockHdr *h, size_t total)
{
    size_t surplus = h->size - total;
    if (surplus < HDR_SIZE + ALIGN) return;

    BlockHdr *tail = (BlockHdr *)((char *)h + total);
    tail->size = surplus; tail->flags = 0; tail->next_free = 0; tail->_pad = 0;
    h->size = total;

    int c = sclass(surplus - HDR_SIZE);
    if (c >= 0) fl_push_small(tail, c); else fl_push_large(tail);
}

/* ── malloc ────────────────────────────────────────────────────────────── */

void *malloc(size_t sz)
{
    if (sz == 0) sz = 1;
    if (!g_ready && heap_init() != 0) return 0;

    size_t    total = total_for(sz);
    int       cls   = sclass(sz);
    BlockHdr *h     = 0;

    if (cls >= 0) {
        /* Try the exact size class, then progressively larger small classes. */
        for (int c = cls; c < (int)NUM_CLASSES; c++) {
            h = fl_pop_small(c);
            if (h) break;
        }
        /* Fall back to large list. */
        if (!h) { h = fl_find_large(total); if (h) fl_remove_large(h); }
    } else {
        h = fl_find_large(total); if (h) fl_remove_large(h);
    }

    if (!h) {
        /* Grow the heap and use the new block. */
        h = heap_grow(total);
        if (!h) return 0;
        /* heap_grow puts it on the large list; take it off. */
        fl_remove_large(h);
    }

    maybe_split(h, total);
    h->flags = 0; h->next_free = 0;
    return (char *)h + HDR_SIZE;
}

/* ── free ──────────────────────────────────────────────────────────────── */

void free(void *ptr)
{
    if (!ptr) return;

    BlockHdr *h = (BlockHdr *)((char *)ptr - HDR_SIZE);
    if (h->flags & BLOCK_FREE) return;   /* double-free guard */

    /* Forward coalesce with next physical block if it's free. */
    BlockHdr *nxt = block_next(h);
    if (!(nxt->flags & BLOCK_SENTINEL) && (nxt->flags & BLOCK_FREE)) {
        fl_remove(nxt);
        h->size += nxt->size;
    }

    int c = sclass(h->size - HDR_SIZE);
    if (c >= 0) fl_push_small(h, c); else fl_push_large(h);
}

/* ── calloc ────────────────────────────────────────────────────────────── */

void *calloc(size_t nmemb, size_t sz)
{
    if (nmemb && sz > (size_t)-1 / nmemb) return 0;
    size_t total = nmemb * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/* ── realloc ───────────────────────────────────────────────────────────── */

void *realloc(void *ptr, size_t sz)
{
    if (!ptr) return malloc(sz);
    if (!sz)  { free(ptr); return 0; }

    BlockHdr *h   = (BlockHdr *)((char *)ptr - HDR_SIZE);
    size_t cur_py = h->size - HDR_SIZE;
    if (sz <= cur_py) return ptr;

    /* Try in-place expansion: absorb the free next block. */
    BlockHdr *nxt = block_next(h);
    if (!(nxt->flags & BLOCK_SENTINEL) && (nxt->flags & BLOCK_FREE)) {
        size_t combined = h->size + nxt->size;
        if (combined >= total_for(sz)) {
            fl_remove(nxt);
            h->size = combined;
            maybe_split(h, total_for(sz));
            return ptr;
        }
    }

    /* Fall back: allocate, copy, free. */
    void *np = malloc(sz);
    if (!np) return 0;
    memcpy(np, ptr, cur_py);
    free(ptr);
    return np;
}