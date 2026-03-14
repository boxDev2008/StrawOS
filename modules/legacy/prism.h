/*  ██████╗ ██████╗ ██╗███████╗███╗   ███╗
 *  ██╔══██╗██╔══██╗██║██╔════╝████╗ ████║
 *  ██████╔╝██████╔╝██║███████╗██╔████╔██║
 *  ██╔═══╝ ██╔══██╗██║╚════██║██║╚██╔╝██║
 *  ██║     ██║  ██║██║███████║██║ ╚═╝ ██║
 *  ╚═╝     ╚═╝  ╚═╝╚═╝╚══════╝╚═╝     ╚═╝
 *
 *  Prism — single-header software rasterizer
 *  OpenGL-like API, pure C, no dependencies.
 *
 *  USAGE
 *  -----
 *  In exactly ONE .c file, before including this header:
 *
 *      #define PRISM_IMPLEMENTATION
 *      #include "prism.h"
 *
 *  In all other files, just:
 *
 *      #include "prism.h"
 *
 *  QUICK START
 *  -----------
 *      // Create context targeting a uint32_t pixel buffer
 *      PrismContext *pr = pr_create_context(pixels, width, height);
 *
 *      // Each frame:
 *      pr_clear(pr, PR_COLOR_BUFFER | PR_DEPTH_BUFFER);
 *      pr_bind_texture(pr, tex_pixels, tex_w, tex_h);
 *      pr_load_matrix(pr, PR_PROJECTION, &proj);
 *      pr_load_matrix(pr, PR_MODELVIEW, &mv);
 *      pr_draw_mesh(pr, &mesh);
 *
 *      // Destroy when done
 *      pr_destroy_context(pr);
 *
 *  LICENSE
 *  -------
 *  Public domain / MIT — use freely.
 */

#ifndef PRISM_H
#define PRISM_H

#include <stdint.h>

/* ── Compile-time options ───────────────────────────────────────────── */
#ifndef PR_MAX_MATRIX_STACK
#  define PR_MAX_MATRIX_STACK 16
#endif

/* ── Types ──────────────────────────────────────────────────────────── */

typedef struct { float x, y, z, w; } PrVec4;
typedef struct { float x, y, z; }    PrVec3;
typedef struct { float x, y; }       PrVec2;
typedef struct { float m[4][4]; }    PrMat4;

/* A single triangle vertex (position + UV) */
typedef struct {
    PrVec3 pos;
    PrVec2 uv;
} PrVertex;

/* A mesh: flat array of triangles (3 vertices each, no index buffer) */
typedef struct {
    PrVertex *verts;     /* length = tri_count * 3 */
    int       tri_count;
} PrMesh;

/* Matrix target for pr_load_matrix / pr_push_matrix / pr_pop_matrix */
typedef enum {
    PR_PROJECTION = 0,
    PR_MODELVIEW  = 1,
} PrMatrixMode;

/* Buffer bits for pr_clear */
typedef enum {
    PR_COLOR_BUFFER = 1 << 0,
    PR_DEPTH_BUFFER = 1 << 1,
} PrClearBits;

/* Cull face mode */
typedef enum {
    PR_CULL_NONE  = 0,
    PR_CULL_BACK  = 1,
    PR_CULL_FRONT = 2,
} PrCullMode;

/* Depth test function */
typedef enum {
    PR_DEPTH_LESS         = 0,   /* default: pass if new < stored */
    PR_DEPTH_LESS_EQUAL   = 1,
    PR_DEPTH_ALWAYS       = 2,
} PrDepthFunc;

/* Texture wrap mode */
typedef enum {
    PR_WRAP_CLAMP  = 0,
    PR_WRAP_REPEAT = 1,
} PrWrapMode;

/* Opaque context — allocate with pr_create_context */
typedef struct PrContext PrContext;

/* ── Context ────────────────────────────────────────────────────────── */
PrContext *pr_create_context(uint32_t *color_buf, uint32_t width, uint32_t height);
void       pr_destroy_context(PrContext *pr);

/* ── Buffer clear ───────────────────────────────────────────────────── */
void pr_clear(PrContext *pr, int bits);
void pr_set_clear_color(PrContext *pr, uint32_t color);   /* ARGB packed */

/* ── Matrix stack ───────────────────────────────────────────────────── */
void pr_matrix_mode(PrContext *pr, PrMatrixMode mode);
void pr_load_identity(PrContext *pr);
void pr_load_matrix(PrContext *pr, PrMatrixMode mode, const PrMat4 *m);
void pr_get_matrix(PrContext *pr, PrMatrixMode mode, PrMat4 *out);
void pr_push_matrix(PrContext *pr);
void pr_pop_matrix(PrContext *pr);
void pr_mult_matrix(PrContext *pr, const PrMat4 *m);

/* Convenience transform helpers (operate on current matrix mode) */
void pr_translate(PrContext *pr, float x, float y, float z);
void pr_scale(PrContext *pr, float x, float y, float z);
void pr_rotate_x(PrContext *pr, float radians);
void pr_rotate_y(PrContext *pr, float radians);
void pr_rotate_z(PrContext *pr, float radians);

/* Built-in matrix constructors */
PrMat4 pr_mat4_identity(void);
PrMat4 pr_mat4_perspective(float fovy, float aspect, float znear, float zfar);
PrMat4 pr_mat4_ortho(float left, float right, float bottom, float top, float znear, float zfar);
PrMat4 pr_mat4_look_at(PrVec3 eye, PrVec3 center, PrVec3 up);
PrMat4 pr_mat4_translate(float x, float y, float z);
PrMat4 pr_mat4_scale(float x, float y, float z);
PrMat4 pr_mat4_rotation_x(float t);
PrMat4 pr_mat4_rotation_y(float t);
PrMat4 pr_mat4_rotation_z(float t);
PrMat4 pr_mat4_mul(PrMat4 a, PrMat4 b);
PrVec4 pr_mat4_mul_vec4(PrMat4 m, PrVec4 v);

/* ── Texture ────────────────────────────────────────────────────────── */
void pr_bind_texture(PrContext *pr, const uint32_t *pixels, int w, int h);
void pr_unbind_texture(PrContext *pr);
void pr_set_wrap(PrContext *pr, PrWrapMode wrap);
void pr_flip_v(PrContext *pr, int enable);   /* flip V axis (OBJ models need this) */

/* ── Render state ───────────────────────────────────────────────────── */
void pr_set_cull_mode(PrContext *pr, PrCullMode mode);
void pr_set_depth_func(PrContext *pr, PrDepthFunc func);
void pr_set_depth_write(PrContext *pr, int enable);

/* ── Draw calls ─────────────────────────────────────────────────────── */

/* Draw a PrMesh — applies current MODELVIEW + PROJECTION matrices */
void pr_draw_mesh(PrContext *pr, const PrMesh *mesh);

/* Draw raw triangles from a flat vertex array (count must be multiple of 3) */
void pr_draw_triangles(PrContext *pr, const PrVertex *verts, int vert_count);

/* Draw a single triangle */
void pr_draw_triangle(PrContext *pr, PrVertex a, PrVertex b, PrVertex c);

/* ── Mesh helpers ───────────────────────────────────────────────────── */
PrMesh pr_mesh_alloc(int tri_count);   /* allocate empty mesh */
void   pr_mesh_free(PrMesh *mesh);

/* OBJ loader — returns a PrMesh. Caller owns memory (pr_mesh_free). */
PrMesh pr_load_obj(const char *filename);

/* ── Stats ──────────────────────────────────────────────────────────── */
typedef struct {
    int tris_submitted;
    int tris_drawn;       /* after culling */
    int pixels_shaded;
} PrStats;

PrStats pr_get_stats(PrContext *pr);
void    pr_reset_stats(PrContext *pr);


/* ═══════════════════════════════════════════════════════════════════════
 *  IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef PRISM_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

#ifdef __SSE__
#  include <immintrin.h>
#  define PR_HAS_SSE 1
#else
#  define PR_HAS_SSE 0
#endif

/* ── Internal fast math ─────────────────────────────────────────────── */
static inline float pr__rcp(float x)
{
#if PR_HAS_SSE
    float r;
    _mm_store_ss(&r, _mm_rcp_ss(_mm_set_ss(x)));
    return r;
#else
    return 1.0f / x;
#endif
}

#ifndef pr__cosf
#  define pr__cosf (float)cos
#  define pr__sinf (float)sin
#  define pr__tanf (float)tan
#  define pr__sqrtf (float)sqrt
#endif

/* ── Context struct ─────────────────────────────────────────────────── */
struct PrContext {
    /* output buffers */
    uint32_t *color_buf;
    float    *depth_buf;
    uint32_t  width, height;

    /* clear state */
    uint32_t clear_color;

    /* matrix stacks */
    PrMatrixMode active_mode;
    PrMat4 proj_stack[PR_MAX_MATRIX_STACK];
    PrMat4 mv_stack  [PR_MAX_MATRIX_STACK];
    int    proj_top, mv_top;

    /* texture */
    const uint32_t *tex_pixels;
    int             tex_w, tex_h;
    float           tex_fw, tex_fh;   /* (w-1), (h-1) as float */
    PrWrapMode      wrap;
    int             flip_v;

    /* render state */
    PrCullMode  cull_mode;
    PrDepthFunc depth_func;
    int         depth_write;

    /* stats */
    PrStats stats;
};

/* ── Context ────────────────────────────────────────────────────────── */
PrContext *pr_create_context(uint32_t *color_buf, uint32_t width, uint32_t height)
{
    PrContext *pr = (PrContext *)calloc(1, sizeof(PrContext));
    if (!pr) return NULL;

    pr->color_buf = color_buf;
    pr->depth_buf = (float *)malloc(width * height * sizeof(float));
    if (!pr->depth_buf) { free(pr); return NULL; }

    pr->width  = width;
    pr->height = height;

    /* Default clear values */
    pr->clear_color = 0xFF000000;
    for (uint32_t i = 0; i < width * height; i++)
        pr->depth_buf[i] = 1.0f;

    /* Identity matrices */
    pr->proj_stack[0] = pr_mat4_identity();
    pr->mv_stack[0]   = pr_mat4_identity();
    pr->proj_top = 0;
    pr->mv_top   = 0;
    pr->active_mode = PR_MODELVIEW;

    /* Default render state */
    pr->cull_mode   = PR_CULL_BACK;
    pr->depth_func  = PR_DEPTH_LESS;
    pr->depth_write = 1;
    pr->wrap        = PR_WRAP_CLAMP;
    pr->flip_v      = 0;

    return pr;
}

void pr_destroy_context(PrContext *pr)
{
    if (!pr) return;
    free(pr->depth_buf);
    free(pr);
}

/* ── Buffer clear ───────────────────────────────────────────────────── */
void pr_set_clear_color(PrContext *pr, uint32_t color) { pr->clear_color = color; }

void pr_clear(PrContext *pr, int bits)
{
    uint32_t n = pr->width * pr->height;
    if (bits & PR_COLOR_BUFFER) {
        uint32_t c = pr->clear_color;
#if PR_HAS_SSE
        __m128i v = _mm_set1_epi32((int)c);
        uint32_t i = 0;
        for (; i + 4 <= n; i += 4)
            _mm_storeu_si128((__m128i *)(pr->color_buf + i), v);
        for (; i < n; i++) pr->color_buf[i] = c;
#else
        for (uint32_t i = 0; i < n; i++) pr->color_buf[i] = c;
#endif
    }
    if (bits & PR_DEPTH_BUFFER) {
#if PR_HAS_SSE
        __m128 v = _mm_set1_ps(1.0f);
        uint32_t i = 0;
        for (; i + 4 <= n; i += 4)
            _mm_storeu_ps(pr->depth_buf + i, v);
        for (; i < n; i++) pr->depth_buf[i] = 1.0f;
#else
        for (uint32_t i = 0; i < n; i++) pr->depth_buf[i] = 1.0f;
#endif
    }
}

/* ── Matrix helpers ─────────────────────────────────────────────────── */
static inline PrMat4 *pr__active_mat(PrContext *pr)
{
    return (pr->active_mode == PR_PROJECTION)
        ? &pr->proj_stack[pr->proj_top]
        : &pr->mv_stack[pr->mv_top];
}

static inline int *pr__active_top(PrContext *pr)
{
    return (pr->active_mode == PR_PROJECTION)
        ? &pr->proj_top : &pr->mv_top;
}

static inline PrMat4 *pr__stack(PrContext *pr)
{
    return (pr->active_mode == PR_PROJECTION)
        ? pr->proj_stack : pr->mv_stack;
}

void pr_matrix_mode(PrContext *pr, PrMatrixMode mode) { pr->active_mode = mode; }

void pr_load_identity(PrContext *pr)
{
    *pr__active_mat(pr) = pr_mat4_identity();
}

void pr_load_matrix(PrContext *pr, PrMatrixMode mode, const PrMat4 *m)
{
    PrMatrixMode prev = pr->active_mode;
    pr->active_mode = mode;
    *pr__active_mat(pr) = *m;
    pr->active_mode = prev;
}

void pr_get_matrix(PrContext *pr, PrMatrixMode mode, PrMat4 *out)
{
    if (mode == PR_PROJECTION) *out = pr->proj_stack[pr->proj_top];
    else                        *out = pr->mv_stack[pr->mv_top];
}

void pr_push_matrix(PrContext *pr)
{
    int *top = pr__active_top(pr);
    PrMat4 *stack = pr__stack(pr);
    if (*top + 1 < PR_MAX_MATRIX_STACK) {
        stack[*top + 1] = stack[*top];
        (*top)++;
    }
}

void pr_pop_matrix(PrContext *pr)
{
    int *top = pr__active_top(pr);
    if (*top > 0) (*top)--;
}

void pr_mult_matrix(PrContext *pr, const PrMat4 *m)
{
    PrMat4 *cur = pr__active_mat(pr);
    *cur = pr_mat4_mul(*cur, *m);
}

void pr_translate(PrContext *pr, float x, float y, float z)
{
    PrMat4 t = pr_mat4_translate(x, y, z);
    pr_mult_matrix(pr, &t);
}

void pr_scale(PrContext *pr, float x, float y, float z)
{
    PrMat4 s = pr_mat4_scale(x, y, z);
    pr_mult_matrix(pr, &s);
}

void pr_rotate_x(PrContext *pr, float r) { PrMat4 m = pr_mat4_rotation_x(r); pr_mult_matrix(pr, &m); }
void pr_rotate_y(PrContext *pr, float r) { PrMat4 m = pr_mat4_rotation_y(r); pr_mult_matrix(pr, &m); }
void pr_rotate_z(PrContext *pr, float r) { PrMat4 m = pr_mat4_rotation_z(r); pr_mult_matrix(pr, &m); }

/* ── Matrix constructors ────────────────────────────────────────────── */
PrMat4 pr_mat4_identity(void)
{
    PrMat4 m = {{{0}}};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

PrMat4 pr_mat4_mul(PrMat4 a, PrMat4 b)
{
    PrMat4 r = {{{0}}};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

PrVec4 pr_mat4_mul_vec4(PrMat4 m, PrVec4 v)
{
    return (PrVec4){
        m.m[0][0]*v.x + m.m[0][1]*v.y + m.m[0][2]*v.z + m.m[0][3]*v.w,
        m.m[1][0]*v.x + m.m[1][1]*v.y + m.m[1][2]*v.z + m.m[1][3]*v.w,
        m.m[2][0]*v.x + m.m[2][1]*v.y + m.m[2][2]*v.z + m.m[2][3]*v.w,
        m.m[3][0]*v.x + m.m[3][1]*v.y + m.m[3][2]*v.z + m.m[3][3]*v.w,
    };
}

PrMat4 pr_mat4_perspective(float fovy, float aspect, float znear, float zfar)
{
    float f = 1.0f / pr__tanf(fovy * 0.5f);
    float d = znear - zfar;
    PrMat4 m = {{{0}}};
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = (zfar + znear) / d;
    m.m[2][3] = (2.0f * zfar * znear) / d;
    m.m[3][2] = -1.0f;
    return m;
}

PrMat4 pr_mat4_ortho(float left, float right, float bottom, float top, float znear, float zfar)
{
    PrMat4 m = {{{0}}};
    m.m[0][0] =  2.0f / (right - left);
    m.m[1][1] =  2.0f / (top - bottom);
    m.m[2][2] = -2.0f / (zfar - znear);
    m.m[0][3] = -(right + left)   / (right - left);
    m.m[1][3] = -(top   + bottom) / (top   - bottom);
    m.m[2][3] = -(zfar  + znear)  / (zfar  - znear);
    m.m[3][3] =  1.0f;
    return m;
}

PrMat4 pr_mat4_look_at(PrVec3 eye, PrVec3 center, PrVec3 up)
{
    /* forward = normalize(center - eye) */
    float fx = center.x-eye.x, fy = center.y-eye.y, fz = center.z-eye.z;
    float fl = pr__rcp(pr__sqrtf(fx*fx + fy*fy + fz*fz));
    fx *= fl; fy *= fl; fz *= fl;

    /* right = normalize(forward x up) */
    float rx = fy*up.z - fz*up.y;
    float ry = fz*up.x - fx*up.z;
    float rz = fx*up.y - fy*up.x;
    float rl = pr__rcp(pr__sqrtf(rx*rx + ry*ry + rz*rz));
    rx *= rl; ry *= rl; rz *= rl;

    /* true_up = right x forward */
    float ux = ry*fz - rz*fy;
    float uy = rz*fx - rx*fz;
    float uz = rx*fy - ry*fx;

    PrMat4 m = {{{0}}};
    m.m[0][0]=rx; m.m[0][1]=ry; m.m[0][2]=rz; m.m[0][3]=-(rx*eye.x+ry*eye.y+rz*eye.z);
    m.m[1][0]=ux; m.m[1][1]=uy; m.m[1][2]=uz; m.m[1][3]=-(ux*eye.x+uy*eye.y+uz*eye.z);
    m.m[2][0]=-fx;m.m[2][1]=-fy;m.m[2][2]=-fz;m.m[2][3]= (fx*eye.x+fy*eye.y+fz*eye.z);
    m.m[3][3]=1.0f;
    return m;
}

PrMat4 pr_mat4_translate(float x, float y, float z)
{
    PrMat4 m = pr_mat4_identity();
    m.m[0][3] = x; m.m[1][3] = y; m.m[2][3] = z;
    return m;
}

PrMat4 pr_mat4_scale(float x, float y, float z)
{
    PrMat4 m = {{{0}}};
    m.m[0][0]=x; m.m[1][1]=y; m.m[2][2]=z; m.m[3][3]=1.0f;
    return m;
}

PrMat4 pr_mat4_rotation_x(float t)
{
    float c = pr__cosf(t), s = pr__sinf(t);
    PrMat4 m = pr_mat4_identity();
    m.m[1][1]= c; m.m[1][2]=-s;
    m.m[2][1]= s; m.m[2][2]= c;
    return m;
}

PrMat4 pr_mat4_rotation_y(float t)
{
    float c = pr__cosf(t), s = pr__sinf(t);
    PrMat4 m = pr_mat4_identity();
    m.m[0][0]= c; m.m[0][2]= s;
    m.m[2][0]=-s; m.m[2][2]= c;
    return m;
}

PrMat4 pr_mat4_rotation_z(float t)
{
    float c = pr__cosf(t), s = pr__sinf(t);
    PrMat4 m = pr_mat4_identity();
    m.m[0][0]= c; m.m[0][1]=-s;
    m.m[1][0]= s; m.m[1][1]= c;
    return m;
}

/* ── Texture ────────────────────────────────────────────────────────── */
void pr_bind_texture(PrContext *pr, const uint32_t *pixels, int w, int h)
{
    pr->tex_pixels = pixels;
    pr->tex_w = w; pr->tex_h = h;
    pr->tex_fw = (float)(w - 1);
    pr->tex_fh = (float)(h - 1);
}

void pr_unbind_texture(PrContext *pr)
{
    pr->tex_pixels = NULL;
    pr->tex_w = pr->tex_h = 0;
}

void pr_set_wrap(PrContext *pr, PrWrapMode wrap)    { pr->wrap   = wrap;   }
void pr_flip_v (PrContext *pr, int enable)          { pr->flip_v = enable; }

/* ── Render state ───────────────────────────────────────────────────── */
void pr_set_cull_mode  (PrContext *pr, PrCullMode m)  { pr->cull_mode   = m; }
void pr_set_depth_func (PrContext *pr, PrDepthFunc f) { pr->depth_func  = f; }
void pr_set_depth_write(PrContext *pr, int enable)    { pr->depth_write = enable; }

/* ── Stats ──────────────────────────────────────────────────────────── */
PrStats pr_get_stats(PrContext *pr)  { return pr->stats; }
void    pr_reset_stats(PrContext *pr){ memset(&pr->stats, 0, sizeof(PrStats)); }

/* ── Core rasterizer ────────────────────────────────────────────────── */

/* Sample texture at (u,v) with current wrap/flip settings */
static inline uint32_t pr__sample(PrContext *pr, float u, float v)
{
    if (!pr->tex_pixels) return 0xFFFF00FF; /* magenta = no texture */

    if (pr->flip_v) v = 1.0f - v;

    if (pr->wrap == PR_WRAP_REPEAT) {
        u = u - (float)(int)u; if (u < 0.0f) u += 1.0f;
        v = v - (float)(int)v; if (v < 0.0f) v += 1.0f;
    } else {
        if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
        if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    }

    int tx = (int)(u * pr->tex_fw);
    int ty = (int)(v * pr->tex_fh);
    return pr->tex_pixels[ty * pr->tex_w + tx];
}

/* Depth test */
static inline int pr__depth_test(PrContext *pr, float new_depth, float stored)
{
    switch (pr->depth_func) {
        case PR_DEPTH_LESS:         return new_depth <  stored;
        case PR_DEPTH_LESS_EQUAL:   return new_depth <= stored;
        case PR_DEPTH_ALWAYS:       return 1;
        default:                    return new_depth <  stored;
    }
}

#define PR__EDGE(ax,ay,bx,by,px,py) \
    (((bx)-(ax))*((py)-(ay))-((by)-(ay))*((px)-(ax)))

/* Rasterize one clip-space triangle */
static void pr__rasterize(PrContext *pr,
    PrVec4 v0, PrVec4 v1, PrVec4 v2,
    float u0, float fv0,
    float u1, float fv1,
    float u2, float fv2)
{
    uint32_t buf_w = pr->width, buf_h = pr->height;

    /* 1. Perspective divide */
    float rw0 = pr__rcp(v0.w), rw1 = pr__rcp(v1.w), rw2 = pr__rcp(v2.w);

    float nx0=v0.x*rw0, ny0=v0.y*rw0, nz0=v0.z*rw0;
    float nx1=v1.x*rw1, ny1=v1.y*rw1, nz1=v1.z*rw1;
    float nx2=v2.x*rw2, ny2=v2.y*rw2, nz2=v2.z*rw2;

    /* 2. Viewport */
    float hw=0.5f*(float)buf_w, hh=0.5f*(float)buf_h;

    float sx0=(nx0+1.0f)*hw, sy0=(1.0f-ny0)*hh, sz0=(nz0+1.0f)*0.5f;
    float sx1=(nx1+1.0f)*hw, sy1=(1.0f-ny1)*hh, sz1=(nz1+1.0f)*0.5f;
    float sx2=(nx2+1.0f)*hw, sy2=(1.0f-ny2)*hh, sz2=(nz2+1.0f)*0.5f;

    int x0=(int)sx0, y0=(int)sy0;
    int x1=(int)sx1, y1=(int)sy1;
    int x2=(int)sx2, y2=(int)sy2;

    /* 3. Bbox */
    int minx=x0, maxx=x0, miny=y0, maxy=y0;
    if(x1<minx)minx=x1; if(x1>maxx)maxx=x1;
    if(x2<minx)minx=x2; if(x2>maxx)maxx=x2;
    if(y1<miny)miny=y1; if(y1>maxy)maxy=y1;
    if(y2<miny)miny=y2; if(y2>maxy)maxy=y2;
    if(minx<0)minx=0; if(miny<0)miny=0;
    if(maxx>=(int)buf_w)maxx=(int)buf_w-1;
    if(maxy>=(int)buf_h)maxy=(int)buf_h-1;
    if(minx>maxx||miny>maxy) return;

    /* 4. Edge setup — normalize to CCW */
    if (PR__EDGE(x0,y0,x1,y1,x2,y2) < 0) {
        int ti;    ti=x1; x1=x2; x2=ti;  ti=y1; y1=y2; y2=ti;
        float tf;  tf=sz1; sz1=sz2; sz2=tf;
                   tf=rw1; rw1=rw2; rw2=tf;
                   tf=u1;  u1=u2;   u2=tf;
                   tf=fv1; fv1=fv2; fv2=tf;
    }

    const int a0=y1-y2, b0=x2-x1;
    const int a1=y2-y0, b1=x0-x2;
    const int a2=y0-y1, b2=x1-x0;

    int w0_row=PR__EDGE(x1,y1,x2,y2,minx,miny);
    int w1_row=PR__EDGE(x2,y2,x0,y0,minx,miny);
    int w2_row=PR__EDGE(x0,y0,x1,y1,minx,miny);

    /* 5. Area */
    int total_area = w0_row + w1_row + w2_row;
    if (total_area <= 0) return;
    float area_inv = pr__rcp((float)total_area);

    /* 6. Perspective-correct attribute gradients */
    float zw0=sz0*rw0, zw1=sz1*rw1, zw2=sz2*rw2;
    float uw0= u0*rw0, uw1= u1*rw1, uw2= u2*rw2;
    float vw0=fv0*rw0, vw1=fv1*rw1, vw2=fv2*rw2;

    #define PR__DX(f0,f1,f2) (((float)a0*(f0)+(float)a1*(f1)+(float)a2*(f2))*area_inv)
    #define PR__DY(f0,f1,f2) (((float)b0*(f0)+(float)b1*(f1)+(float)b2*(f2))*area_inv)
    #define PR__EV(f0,f1,f2) (((float)w0_row*(f0)+(float)w1_row*(f1)+(float)w2_row*(f2))*area_inv)

    float d_zw_dx=PR__DX(zw0,zw1,zw2), d_zw_dy=PR__DY(zw0,zw1,zw2);
    float d_rw_dx=PR__DX(rw0,rw1,rw2), d_rw_dy=PR__DY(rw0,rw1,rw2);
    float d_uw_dx=PR__DX(uw0,uw1,uw2), d_uw_dy=PR__DY(uw0,uw1,uw2);
    float d_vw_dx=PR__DX(vw0,vw1,vw2), d_vw_dy=PR__DY(vw0,vw1,vw2);

    float zw_row=PR__EV(zw0,zw1,zw2), rw_row=PR__EV(rw0,rw1,rw2);
    float uw_row=PR__EV(uw0,uw1,uw2), vw_row=PR__EV(vw0,vw1,vw2);

    #undef PR__DX
    #undef PR__DY
    #undef PR__EV

    /* 7. Scan */
    uint32_t *row_ptr   = pr->color_buf + (uint32_t)miny * buf_w;
    float    *depth_row = pr->depth_buf + (uint32_t)miny * buf_w;

    for (int py=miny; py<=maxy; py++,
         row_ptr+=buf_w, depth_row+=buf_w,
         w0_row+=b0, w1_row+=b1, w2_row+=b2,
         zw_row+=d_zw_dy, rw_row+=d_rw_dy,
         uw_row+=d_uw_dy, vw_row+=d_vw_dy)
    {
        int w0e=w0_row+a0*(maxx-minx);
        int w1e=w1_row+a1*(maxx-minx);
        int w2e=w2_row+a2*(maxx-minx);
        if ((w0_row<0&&w0e<0)||(w1_row<0&&w1e<0)||(w2_row<0&&w2e<0)) continue;

        int   w0=w0_row, w1=w1_row, w2=w2_row;
        float zw=zw_row, rw=rw_row, uw=uw_row, vw=vw_row;

        for (int px=minx; px<=maxx; px++,
             w0+=a0, w1+=a1, w2+=a2,
             zw+=d_zw_dx, rw+=d_rw_dx,
             uw+=d_uw_dx, vw+=d_vw_dx)
        {
            if (w0<0||w1<0||w2<0) continue;

            float depth = zw * pr__rcp(rw);
            if (depth < 0.0f || depth > 1.0f) continue;

            if (!pr__depth_test(pr, depth, depth_row[px])) continue;

            float rcp_rw = pr__rcp(rw);
            float u = uw * rcp_rw;
            float v = vw * rcp_rw;

            uint32_t texel = pr__sample(pr, u, v);

            if (pr->depth_write) depth_row[px] = depth;
            row_ptr[px] = texel;
            pr->stats.pixels_shaded++;
        }
    }
}
#undef PR__EDGE

/* ── Back-face cull ─────────────────────────────────────────────────── */
static inline int pr__should_cull(PrContext *pr, PrVec4 a, PrVec4 b, PrVec4 c)
{
    if (pr->cull_mode == PR_CULL_NONE) return 0;

    float ax=b.x/b.w-a.x/a.w, ay=b.y/b.w-a.y/a.w;
    float bx=c.x/c.w-a.x/a.w, by=c.y/c.w-a.y/a.w;
    float cross = ax*by - ay*bx;

    if (pr->cull_mode == PR_CULL_BACK)  return cross < 0.0f;
    if (pr->cull_mode == PR_CULL_FRONT) return cross > 0.0f;
    return 0;
}

/* ── Draw calls ─────────────────────────────────────────────────────── */
void pr_draw_triangle(PrContext *pr, PrVertex a, PrVertex b, PrVertex c)
{
    PrMat4 mv   = pr->mv_stack[pr->mv_top];
    PrMat4 proj = pr->proj_stack[pr->proj_top];
    PrMat4 mvp  = pr_mat4_mul(proj, mv);

    PrVec4 va = {a.pos.x, a.pos.y, a.pos.z, 1.0f};
    PrVec4 vb = {b.pos.x, b.pos.y, b.pos.z, 1.0f};
    PrVec4 vc = {c.pos.x, c.pos.y, c.pos.z, 1.0f};

    va = pr_mat4_mul_vec4(mvp, va);
    vb = pr_mat4_mul_vec4(mvp, vb);
    vc = pr_mat4_mul_vec4(mvp, vc);

    pr->stats.tris_submitted++;
    if (va.w<=0.0f||vb.w<=0.0f||vc.w<=0.0f) return;
    if (pr__should_cull(pr, va, vb, vc)) return;

    pr->stats.tris_drawn++;
    pr__rasterize(pr, va, vb, vc,
                  a.uv.x, a.uv.y,
                  b.uv.x, b.uv.y,
                  c.uv.x, c.uv.y);
}

void pr_draw_triangles(PrContext *pr, const PrVertex *verts, int vert_count)
{
    PrMat4 mv   = pr->mv_stack[pr->mv_top];
    PrMat4 proj = pr->proj_stack[pr->proj_top];
    PrMat4 mvp  = pr_mat4_mul(proj, mv);

    for (int i = 0; i + 2 < vert_count; i += 3) {
        const PrVertex *a = &verts[i];
        const PrVertex *b = &verts[i+1];
        const PrVertex *c = &verts[i+2];

        PrVec4 va = {a->pos.x, a->pos.y, a->pos.z, 1.0f};
        PrVec4 vb = {b->pos.x, b->pos.y, b->pos.z, 1.0f};
        PrVec4 vc = {c->pos.x, c->pos.y, c->pos.z, 1.0f};

        va = pr_mat4_mul_vec4(mvp, va);
        vb = pr_mat4_mul_vec4(mvp, vb);
        vc = pr_mat4_mul_vec4(mvp, vc);

        pr->stats.tris_submitted++;
        if (va.w<=0.0f||vb.w<=0.0f||vc.w<=0.0f) continue;
        if (pr__should_cull(pr, va, vb, vc)) continue;

        pr->stats.tris_drawn++;
        pr__rasterize(pr, va, vb, vc,
                      a->uv.x, a->uv.y,
                      b->uv.x, b->uv.y,
                      c->uv.x, c->uv.y);
    }
}

void pr_draw_mesh(PrContext *pr, const PrMesh *mesh)
{
    pr_draw_triangles(pr, mesh->verts, mesh->tri_count * 3);
}

/* ── Mesh helpers ───────────────────────────────────────────────────── */
PrMesh pr_mesh_alloc(int tri_count)
{
    PrMesh m;
    m.tri_count = tri_count;
    m.verts = (PrVertex *)malloc(tri_count * 3 * sizeof(PrVertex));
    return m;
}

void pr_mesh_free(PrMesh *mesh)
{
    free(mesh->verts);
    mesh->verts = NULL;
    mesh->tri_count = 0;
}

/* ── OBJ loader ─────────────────────────────────────────────────────── */
static float pr__atof(const char *s)
{
    while (*s==' '||*s=='\t') s++;
    float sign=1.0f;
    if (*s=='-'){sign=-1.0f;s++;} else if (*s=='+') s++;
    float v=0.0f;
    while (*s>='0'&&*s<='9') v=v*10.0f+(*s++-'0');
    if (*s=='.'){s++;float f=0.1f;while(*s>='0'&&*s<='9'){v+=(*s++-'0')*f;f*=0.1f;}}
    if (*s=='e'||*s=='E'){
        s++;int es=1;
        if(*s=='-'){es=-1;s++;}else if(*s=='+')s++;
        int ex=0; while(*s>='0'&&*s<='9') ex=ex*10+(*s++-'0');
        float p=1.0f; for(int i=0;i<ex;i++) p*=10.0f;
        if(es>0) v*=p; else v/=p;
    }
    return sign*v;
}

static int pr__atoi(const char *s)
{
    while (*s==' '||*s=='\t') s++;
    int sign=1,v=0;
    if(*s=='-'){sign=-1;s++;}
    while(*s>='0'&&*s<='9') v=v*10+(*s++-'0');
    return sign*v;
}

static const char *pr__next_tok(const char *s)
{
    while (*s&&*s!=' '&&*s!='\t'&&*s!='\r'&&*s!='\n') s++;
    while (*s==' '||*s=='\t') s++;
    return s;
}

static const char *pr__parse_corner(const char *s, int *vi, int *ti)
{
    *vi=pr__atoi(s)-1; *ti=-1;
    while(*s&&*s!='/'&&*s!=' '&&*s!='\t'&&*s!='\r'&&*s!='\n') s++;
    if(*s=='/'){s++;if(*s!='/') *ti=pr__atoi(s)-1;}
    while(*s&&*s!=' '&&*s!='\t'&&*s!='\r'&&*s!='\n') s++;
    while(*s==' '||*s=='\t') s++;
    return s;
}

PrMesh pr_load_obj(const char *filename)
{
    PrMesh mesh = {0};

    int fd = open(filename, O_RDONLY);
    if (!fd) return mesh;
    stat_t st;
    fstat(fd, &st);

    char *buf = malloc(st.st_size + 1);
    if (!buf) { close(fd); return mesh; }
    read(fd, buf, st.st_size);
    close(fd);
    buf[st.st_size] = '\0';

    /* Count */
    int nv=0, nvt=0, nf=0;
    for (const char *p=buf;*p;) {
        while(*p==' '||*p=='\t') p++;
        if     (p[0]=='v'&&p[1]==' ')  nv++;
        else if(p[0]=='v'&&p[1]=='t')  nvt++;
        else if(p[0]=='f'&&p[1]==' ')  nf++;
        while(*p&&*p!='\n') p++;
        if(*p=='\n') p++;
    }

    PrVec3 *verts = (PrVec3 *)malloc(nv  * sizeof(PrVec3));
    PrVec2 *uvs   = (PrVec2 *)malloc(nvt * sizeof(PrVec2));
    mesh.verts    = (PrVertex*)malloc(nf * 2 * 3 * sizeof(PrVertex));
    if (!verts||!uvs||!mesh.verts) { free(buf); return mesh; }

    int vi=0, ti=0;
    mesh.tri_count = 0;

    /* Parse */
    for (const char *p=buf;*p;) {
        while(*p==' '||*p=='\t') p++;
        if (p[0]=='v'&&p[1]==' ') {
            p+=2;
            verts[vi].x=pr__atof(p); p=pr__next_tok(p);
            verts[vi].y=pr__atof(p); p=pr__next_tok(p);
            verts[vi].z=pr__atof(p);
            vi++;
        } else if (p[0]=='v'&&p[1]=='t') {
            p+=3;
            uvs[ti].x=pr__atof(p); p=pr__next_tok(p);
            uvs[ti].y=pr__atof(p);
            ti++;
        } else if (p[0]=='f'&&p[1]==' ') {
            p+=2;
            int cv[4],ct[4],nc=0;
            while(*p&&*p!='\r'&&*p!='\n'&&nc<4)
                p=pr__parse_corner(p,&cv[nc],&ct[nc]),nc++;
            for (int i=1;i+1<nc;i++) {
                int idx[3]={0,i,i+1};
                for (int k=0;k<3;k++) {
                    PrVertex *vt = &mesh.verts[mesh.tri_count*3 + k];
                    vt->pos = verts[cv[idx[k]]];
                    vt->uv  = (ct[idx[k]]>=0) ? uvs[ct[idx[k]]] : (PrVec2){0,0};
                }
                mesh.tri_count++;
            }
        }
        while(*p&&*p!='\n') p++;
        if(*p=='\n') p++;
    }

    free(verts); free(uvs); free(buf);
    return mesh;
}

#endif /* PRISM_IMPLEMENTATION */
#endif /* PRISM_H */