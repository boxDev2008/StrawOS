#include <libc.h>
#include <dev.h>

#define NK_ASSERT
#define STBTT_assert
#define assert(x) ((void)(x))

/* ── Nuklear configuration ──────────────────────────────────────────── */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_RAWFB_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_rawfb.h"

/* ── Prism ──────────────────────────────────────────────────────────── */
#define PRISM_IMPLEMENTATION
#include "prism.h"

#include <immintrin.h>

extern uint32_t* load_bmp_from_file(const char *filename, int *out_width, int *out_height);

static void blit(uint32_t *dst, const uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch_bytes)
{
    const uint32_t src_row_bytes = w * sizeof(uint32_t);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t       *d = (uint8_t *)dst + (size_t)y * dst_pitch_bytes;
        const uint32_t *s = src + (size_t)y * w;

        uint32_t x = 0;
        for (; x + 4 <= w; x += 4)
            _mm_stream_si128((__m128i *)(d + x * 4), _mm_load_si128((const __m128i *)(s + x)));
        for (; x < w; x++)
            ((uint32_t *)d)[x] = s[x];
    }
    _mm_sfence();
}

/* ── main ───────────────────────────────────────────────────────────── */
int main(void)
{
    /* 1. Get framebuffer */
    FramebufferDevice fb;
    device(DEVICE_FRAMEBUFFER, &fb);

    const uint32_t w = fb.width;
    const uint32_t h = fb.height;

    /* 2. Allocate 16-byte aligned backbuffer */
    void     *bb_raw    = mmap(h * w * sizeof(uint32_t) + 15, PROT_WRITE);
    if (!bb_raw) return 1;
    uint32_t *backbuffer = (uint32_t *)(((uintptr_t)bb_raw + 15) & ~(uintptr_t)15);

    /* 4. Initialise rawfb */
    #define FONT_ATLAS_W 512
    #define FONT_ATLAS_H 512
    void *tex_mem = calloc(1, FONT_ATLAS_W * FONT_ATLAS_H * 4);

    if (!tex_mem) return 1;

    struct rawfb_pl pl = {
        .bytesPerPixel = 4,
        .rshift = 16, .gshift = 8, .bshift = 0, .ashift = 24,
        .rloss  = 0,  .gloss  = 0, .bloss  = 0, .aloss  = 0,
    };
    struct rawfb_context *rawfb = nk_rawfb_init(backbuffer, tex_mem, w, h, w * sizeof(uint32_t), pl);
    if (!rawfb) return 1;

    struct nk_context *ctx = &rawfb->ctx;

    /* 5. Theme */
    struct nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_WINDOW]                   = nk_rgba(40, 40, 58, 255);
    table[NK_COLOR_HEADER]                   = nk_rgba(32, 32, 48, 255);
    table[NK_COLOR_BORDER]                   = nk_rgba(80, 82, 105, 255);
    table[NK_COLOR_BUTTON]                   = nk_rgba(58, 58, 80, 255);
    table[NK_COLOR_BUTTON_HOVER]             = nk_rgba(90, 90, 120, 255);
    table[NK_COLOR_BUTTON_ACTIVE]            = nk_rgba(120, 122, 160, 255);
    table[NK_COLOR_TEXT]                     = nk_rgba(220, 225, 255, 255);
    table[NK_COLOR_SELECT]                   = nk_rgba(58, 58, 80, 255);
    table[NK_COLOR_SELECT_ACTIVE]            = nk_rgba(160, 162, 210, 255);
    table[NK_COLOR_SCROLLBAR]               = nk_rgba(40, 40, 58, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(100, 102, 140, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(130, 132, 175, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(160, 162, 210, 255);
    table[NK_COLOR_TAB_HEADER]              = nk_rgba(32, 32, 48, 255);
    table[NK_COLOR_TOGGLE]                  = nk_rgba(58, 58, 80, 255);
    table[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(100, 102, 140, 255);
    table[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(160, 162, 210, 255);
    table[NK_COLOR_SLIDER]                  = nk_rgba(58, 58, 80, 255);
    table[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(160, 162, 210, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(180, 182, 225, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(200, 202, 240, 255);
    table[NK_COLOR_PROPERTY]               = nk_rgba(48, 48, 68, 255);
    table[NK_COLOR_EDIT]                   = nk_rgba(48, 48, 68, 255);
    table[NK_COLOR_EDIT_CURSOR]            = nk_rgba(220, 225, 255, 255);
    table[NK_COLOR_COMBO]                  = nk_rgba(58, 58, 80, 255);
    table[NK_COLOR_CHART]                  = nk_rgba(48, 48, 68, 255);
    table[NK_COLOR_CHART_COLOR]            = nk_rgba(160, 162, 210, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT]  = nk_rgba(200, 202, 240, 255);

    nk_style_from_table(ctx, table);

    /* 6. Load assets */
    int wallpaper_w, wallpaper_h;
    uint32_t *wallpaper = load_bmp_from_file("/modules/cabin.bmp", &wallpaper_w, &wallpaper_h);
    PrContext *pr = pr_create_context(backbuffer, w, h);

    int room_tex_w, room_tex_h;
    uint32_t *room_tex = load_bmp_from_file("/modules/viking_room.bmp", &room_tex_w, &room_tex_h);
    PrMesh room_mesh = pr_load_obj("/modules/viking_room.obj");

    /* Set up projection matrix (only needs doing once) */
    PrMat4 proj = pr_mat4_perspective(
        0.785398f,                        /* 45° FOV in radians */
        (float)w / (float)h,
        0.1f, 100.0f
    );
    pr_load_matrix(pr, PR_PROJECTION, &proj);

    float room_angle = 0.0f;


    /* ── Main loop ──────────────────────────────────────────────────── */
    while (1)
    {
        MouseDevice mouse;
        device(DEVICE_PS2MOUSE, &mouse);

        nk_input_begin(ctx);
        nk_input_motion(ctx, mouse.x, mouse.y);
        nk_input_button(ctx, NK_BUTTON_LEFT, mouse.x, mouse.y, mouse.buttons & 1);
        nk_input_end(ctx);

        /* Nuklear windows */
        if (nk_begin(ctx, "StrawOS", nk_rect(50, 50, 300, 200),
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE))
        {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label(ctx, "Hello from Nuklear!", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 30, 2);
            if (nk_button_label(ctx, "Button A")) {}
            if (nk_button_label(ctx, "Button B")) {}
        }
        nk_end(ctx);

        if (nk_begin(ctx, "Terminal", nk_rect(400, 50, 300, 200),
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE))
        {
        }
        nk_end(ctx);

        nk_style_push_float(ctx, &ctx->style.window.rounding, 0);
        if (nk_begin(ctx, "Taskbar", nk_rect(0, 0, w, 32),
            NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
        {
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_label(ctx, "StrawOS Taskbar", NK_TEXT_LEFT);
        }
        nk_end(ctx);
        nk_style_pop_float(ctx);

        /* ── Draw wallpaper ─────────────────────────────────────────── */
        for (uint32_t i = 0; i < w * h; i++)
            backbuffer[i] = 0;

        /* ── Draw viking room on top of wallpaper ───────────────────── */
        pr_clear(pr, PR_DEPTH_BUFFER);   /* clear depth only — wallpaper is already in color buf */

        pr_bind_texture(pr, room_tex, room_tex_w, room_tex_h);
        pr_flip_v(pr, 1);                /* OBJ UVs are flipped relative to BMP */
        pr_set_wrap(pr, PR_WRAP_CLAMP);
        pr_set_cull_mode(pr, PR_CULL_BACK);

        /* Build modelview: pull camera back and slightly up, orbit around Y */
        PrMat4 view = pr_mat4_look_at(
            (PrVec3){ 2.0f, 1.5f, 2.0f },   /* eye    */
            (PrVec3){ 0.0f, 0.0f, 0.0f },   /* center */
            (PrVec3){ 0.0f, 1.0f, 0.0f }    /* up     */
        );
        PrMat4 model = pr_mat4_rotation_y(room_angle);
        model = pr_mat4_mul(pr_mat4_scale(0.5f, 0.5f, 0.5f), model); /* move room down so floor is at y=0 */
        PrMat4 mv    = pr_mat4_mul(view, model);
        pr_load_matrix(pr, PR_MODELVIEW, &mv);

        pr_draw_mesh(pr, &room_mesh);

        room_angle += 0.01f;   /* slow rotation each frame */

        /* Composite Nuklear UI on top */
        nk_rawfb_render(rawfb, (struct nk_color){0}, 0);

        /* Flip to framebuffer */
        blit(fb.address, backbuffer, w, h, fb.pitch);
        yield();
    }


    nk_rawfb_shutdown(rawfb);
    pr_mesh_free(&room_mesh);
    pr_destroy_context(pr);
    munmap(bb_raw, h * w * sizeof(uint32_t) + 15);
    return 0;
}