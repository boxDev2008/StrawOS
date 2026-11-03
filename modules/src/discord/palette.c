#include "discord.h"

Palette pal;

bool pal_refresh(void)
{
    const SxTheme *t = sx_theme;
    if (pal.src == t)
        return false;
    pal.src = t;

    pal.main      = t->surface;
    pal.sidebar   = sx_mix(t->bg, t->surface, 90);
    pal.guildbar  = sx_darken(t->bg, 40);
    pal.panel     = sx_mix(pal.sidebar, pal.guildbar, 140);
    pal.input     = sx_mix(t->surface, t->surface_hi, 190);
    pal.hover     = sx_mix(pal.sidebar, t->surface_hi, 140);
    pal.selected  = sx_mix(t->surface_hi, t->border, 120);
    pal.msg_hover = sx_mix(t->surface, t->bg, 80);
    pal.divider   = sx_mix(t->bg, pal.guildbar, 140);
    pal.shadow    = (sx_darken(t->bg, 160) & 0xFFFFFFu) | 0x50000000u;

    pal.header = t->text;
    pal.text   = sx_mix(t->text, t->text_dim, 50);
    pal.muted  = t->text_dim;
    pal.faint  = sx_mix(t->text_dim, t->surface, 100);

    pal.accent     = t->accent;
    pal.accent_hi  = sx_lighten(t->accent, 36);
    pal.on_accent  = t->accent_text;
    pal.link       = t->blue;
    pal.mention_bg = sx_mix(t->surface, t->accent, 64);
    pal.mention    = sx_mix(t->accent, t->text, 90);
    pal.code_bg    = sx_mix(t->surface, pal.guildbar, 120);
    pal.quote      = t->border;
    pal.embed      = sx_mix(t->surface, t->bg, 120);

    pal.red     = t->red;
    pal.red_hi  = sx_darken(t->red, 36);
    pal.green   = t->green;
    pal.yellow  = t->ansi[3];
    pal.tooltip = sx_darken(t->bg, 70);
    return true;
}
