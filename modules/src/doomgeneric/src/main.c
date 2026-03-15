#include "doomgeneric.h"
#include "doomkeys.h"
#include <libc.h>
#include <syscall.h>
#include <dev.h>

#include <immintrin.h>

static uint32_t *backbuffer = NULL;
static FramebufferDevice fb;

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

void DG_Init(void)
{

}

void DG_DrawFrame(void)
{
    if (!backbuffer) return;
    for (uint32_t i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++)
        backbuffer[i] = DG_ScreenBuffer[i];
    blit(fb.address, backbuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY, fb.pitch);
}

uint32_t DG_GetTicksMs(void)
{
    uint32_t ticks = (uint32_t)syscall0(SYS_TIME);
	return ticks;
}


void DG_SleepMs(uint32_t ms)
{
	uint32_t start = DG_GetTicksMs();
    while (DG_GetTicksMs() - start < ms);
        //yield();
}

static unsigned char scancode_to_doom(uint8_t sc)
{
    switch (sc) {
        /* Arrow / movement */
        case 0x48: return KEY_UPARROW;
        case 0x50: return KEY_DOWNARROW;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;

        /* Action */
        case 0x1C: return KEY_ENTER;
        case 0x01: return KEY_ESCAPE;
        case 0x39: return KEY_USE;
        case 0x0F: return KEY_TAB;

        /* Weapons 1-7 */
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x15: return 'y';

        /* Strafe / run */
        case 0x2A: /* L-Shift */
        case 0x36: return KEY_RSHIFT;
        case 0x1D: return KEY_FIRE;   /* L-Ctrl = fire */
        case 0x38: return KEY_LALT;    /* L-Alt  = strafe */

        /* WASD – map to arrows so Doom's default binds work */
        case 0x11: return KEY_UPARROW;    /* W */
        case 0x1F: return KEY_DOWNARROW;  /* S */
        case 0x1E: return KEY_LEFTARROW;  /* A */
        case 0x20: return KEY_RIGHTARROW; /* D */

        /* Comma/period = strafe left/right */
        case 0x33: return KEY_STRAFE_L;
        case 0x34: return KEY_STRAFE_R;

        /* Misc */
        case 0x0E: return KEY_BACKSPACE;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
        case 0x4F: return KEY_END;
        case 0x47: return KEY_HOME;

        default:   return 0;
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    uint8_t scancode;
    while (device(DEVICE_PS2KEYBOARD, &scancode)) {
        bool released   = (scancode & 0x80) != 0;
        uint8_t make_sc = scancode & ~0x80;

        unsigned char dk = scancode_to_doom(make_sc);
        if (dk == 0) continue;   /* unmapped – try next event */

        *pressed = released ? 0 : 1;
        *doomKey  = dk;
        return 1;
    }
    return 0;   /* queue empty */
}

void DG_SetWindowTitle(const char * title)
{

}

int main(void)
{
    device(DEVICE_FRAMEBUFFER, &fb);

    const uint32_t w = DOOMGENERIC_RESX;
    const uint32_t h = DOOMGENERIC_RESY;

    void *bb_raw = mmap(h * w * sizeof(uint32_t) + 15, PROT_WRITE);
    if (!bb_raw) return 1;
    backbuffer = (uint32_t *)(((uintptr_t)bb_raw + 15) & ~(uintptr_t)15);

    char *argv[3] = {"doomgeneric", "-iwad", "/modules/doom1.wad"};
    doomgeneric_Create(3, argv);

    while (1)
    {
        doomgeneric_Tick();
    }
    
    munmap(bb_raw, h * w * sizeof(uint32_t));
    return 0;
}