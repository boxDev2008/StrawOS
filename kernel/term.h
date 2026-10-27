void term_init(void);
void term_set_fg(uint32_t color);
void term_set_bg(uint32_t color);
void term_clear(uint32_t color);
void term_putchar(char c);
void term_puts(const char *s, size_t count);
void term_set_fb_enabled(bool enabled);
