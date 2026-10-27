void sx_ui_button(SxCanvas *c, SxRect r, const char *label, const SxFont *font,
                  SxButtonState state, uint32_t base, uint32_t text)
{
    uint32_t fill = base;
    if (state == SX_BUTTON_HOVER)
        fill = sx_lighten(base, 24);
    else if (state == SX_BUTTON_PRESSED)
        fill = sx_darken(base, 40);

    int radius = r.h / 4 < 8 ? r.h / 4 : 8;
    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, radius, fill);
    if (state != SX_BUTTON_PRESSED)
        sx_fill_rect(c, r.x + radius, r.y, r.w - 2 * radius, 1, SX_RGBA(255, 255, 255, 28));

    SxRect label_box = r;
    if (state == SX_BUTTON_PRESSED)
        label_box.y += 1;
    sx_draw_text_centered(c, font, label_box, label, text);
}

void sx_ui_panel(SxCanvas *c, SxRect r, int radius, uint32_t fill)
{
    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, radius, fill);
    sx_draw_rounded_rect(c, r.x, r.y, r.w, r.h, radius, SX_RGBA(255, 255, 255, 18));
}
