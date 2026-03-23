#include <stdlib.h>
#include <string.h>

#define ALIGN        16u
#define HDR_SIZE     sizeof(BlockHdr)

/* Small size classes: payload 16, 32, 48 … 512 bytes. */
#define SMALL_GRAN   16u
#define MAX_SMALL    512u
#define NUM_CLASSES  (MAX_SMALL / SMALL_GRAN)  /* 32 */

/* Minimum arena size when mmap'ing a new slab. */
#define ARENA_MIN    (4096u * 4u)   /* 16 KiB */

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

/*
 * Arena header — stored at the very start of every mmap'd slab.
 * Each mmap call creates one independent arena; arenas are never merged.
 *
 * Slab layout:
 *   [ Arena (padded to ALIGN) | BlockHdr ... user data ... | sentinel BlockHdr ]
 */
typedef struct Arena {
    struct Arena *next;   /* intrusive linked list of all arenas */
    size_t        size;   /* total bytes including this header   */
} Arena;

/* Offset from the Arena base to the first BlockHdr, keeping ALIGN alignment. */
#define ARENA_HDR_SIZE  ((sizeof(Arena) + ALIGN - 1) & ~(ALIGN - 1))

static int       g_ready;
static Arena    *g_arenas;                /* list of all live slabs          */
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

/* ── arena allocation ──────────────────────────────────────────────────── */

/*
 * mmap a new slab large enough to hold at least `min_payload` bytes,
 * register it in g_arenas, and return the initial free BlockHdr inside it.
 * Returns NULL on failure.
 */
static BlockHdr *arena_new(size_t min_payload)
{
    /* Total bytes we need: arena header + one block + sentinel */
    size_t need = ARENA_HDR_SIZE + total_for(min_payload) + HDR_SIZE;

    /* Round up to a multiple of 4 KiB; always at least ARENA_MIN. */
    size_t min_slab = ARENA_HDR_SIZE + ARENA_MIN + HDR_SIZE;
    size_t slab_size = au(need < min_slab ? min_slab : need, 4096u);

    void *mem = mmap(slab_size, PROT_READ | PROT_WRITE);
    if (!mem || mem == (void *)(size_t)-1) return 0;

    /* Initialise the arena header at the base of the slab. */
    Arena *a = (Arena *)mem;
    a->next  = g_arenas;
    a->size  = slab_size;
    g_arenas = a;

    /* The single free block starts right after the arena header. */
    char     *blk_start = (char *)mem + ARENA_HDR_SIZE;
    char     *blk_end   = (char *)mem + slab_size - HDR_SIZE;

    BlockHdr *blk = (BlockHdr *)blk_start;
    blk->size     = (size_t)(blk_end - blk_start);
    blk->flags    = 0; blk->next_free = 0; blk->_pad = 0;

    sentinel_write(blk_end);

    return blk;
}

/* ── heap init ─────────────────────────────────────────────────────────── */

static int heap_init(void)
{
    BlockHdr *blk = arena_new(ARENA_MIN);
    if (!blk) return -1;
    fl_push_large(blk);
    g_ready = 1;
    return 0;
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
        /*
         * No suitable free block anywhere — mmap a new arena.
         * Push the new block onto the large list and then pull it back off
         * so maybe_split can carve off the remainder normally.
         */
        BlockHdr *blk = arena_new(sz);
        if (!blk) return 0;
        fl_push_large(blk);
        h = fl_find_large(total);
        if (h) fl_remove_large(h);
        if (!h) return 0;   /* should not happen */
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

    /*
     * Forward-coalesce with the next physical block if it is free.
     * Safe because both blocks are in the same arena slab — we never
     * coalesce across arenas because each arena ends in a sentinel
     * (size == 0, BLOCK_SENTINEL set).
     */
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
    /*
     * mmap'd pages are zero-initialised by the kernel, so freshly-carved
     * blocks from a brand-new arena are already zeroed.  Blocks recycled
     * from the free list are not, so we always memset to be safe.
     */
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

    /* Try in-place expansion: absorb the free next block (same arena). */
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

    /* Fall back: allocate new, copy, free old. */
    void *np = malloc(sz);
    if (!np) return 0;
    memcpy(np, ptr, cur_py);
    free(ptr);
    return np;
}