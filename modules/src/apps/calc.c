#include <stdio.h>
#include <string.h>
#include <math.h>
#include <strawx.h>
#include <ui.h>

#define WIN_W     312
#define WIN_H     440
#define DISPLAY_H 120
#define GAP       8
#define COLS      4
#define ROWS      5

typedef enum { K_DIGIT, K_DOT, K_OP, K_EQ, K_CLEAR, K_SIGN, K_PERCENT, K_BACK } KeyKind;

typedef struct
{
    const char *label;
    KeyKind     kind;
    char        value;
    int         col, row, span;
}
CalcKey;

static const CalcKey keys[] = {
    { "C",  K_CLEAR,   0,   0, 0, 1 }, { "+/-", K_SIGN, 0,   1, 0, 1 }, { "%", K_PERCENT, 0, 2, 0, 1 }, { "/", K_OP, '/', 3, 0, 1 },
    { "7",  K_DIGIT,  '7',  0, 1, 1 }, { "8",  K_DIGIT, '8', 1, 1, 1 }, { "9", K_DIGIT, '9', 2, 1, 1 }, { "x", K_OP, '*', 3, 1, 1 },
    { "4",  K_DIGIT,  '4',  0, 2, 1 }, { "5",  K_DIGIT, '5', 1, 2, 1 }, { "6", K_DIGIT, '6', 2, 2, 1 }, { "-", K_OP, '-', 3, 2, 1 },
    { "1",  K_DIGIT,  '1',  0, 3, 1 }, { "2",  K_DIGIT, '2', 1, 3, 1 }, { "3", K_DIGIT, '3', 2, 3, 1 }, { "+", K_OP, '+', 3, 3, 1 },
    { "0",  K_DIGIT,  '0',  0, 4, 2 }, { ".",  K_DOT,    0,  2, 4, 1 }, { "=", K_EQ,    0,  3, 4, 1 },
};
#define KEY_N ((int)(sizeof(keys) / sizeof(keys[0])))

static struct
{
    SxWindow *win;
    char   entry[32];
    double acc;
    char   op;
    bool   fresh;
    bool   error;
    char   history[64];
    int    hover, pressed;
}
C;

static void format_number(double v, char *out, size_t size)
{
    if (isnan(v) || isinf(v) || fabs(v) >= 1e15)
    {
        snprintf(out, size, "Error");
        return;
    }

    bool neg = v < 0;
    if (neg)
        v = -v;

    int int_digits = 1;
    for (double t = v; t >= 10.0; t /= 10.0)
        int_digits++;
    int decimals = 10 - int_digits;
    if (decimals < 0)
        decimals = 0;

    double scale = 1;
    for (int i = 0; i < decimals; i++)
        scale *= 10;
    double r = floor(v * scale + 0.5);
    unsigned long long ip = (unsigned long long)(r / scale);
    unsigned long long fp = (unsigned long long)(r - (double)ip * scale);

    char frac[24] = "";
    if (decimals > 0 && fp > 0)
    {
        for (int i = decimals - 1; i >= 0; i--)
        {
            frac[i] = (char)('0' + fp % 10);
            fp /= 10;
        }
        frac[decimals] = '\0';
        int n = decimals;
        while (n > 0 && frac[n - 1] == '0')
            frac[--n] = '\0';
    }

    if (frac[0])
        snprintf(out, size, "%s%llu.%s", neg ? "-" : "", ip, frac);
    else
        snprintf(out, size, "%s%llu", neg && ip ? "-" : "", ip);
}

static double entry_value(void)
{
    double v = 0, scale = 0;
    bool neg = false;
    for (const char *p = C.entry; *p; p++)
    {
        if (*p == '-')
            neg = true;
        else if (*p == '.')
            scale = 1;
        else if (scale > 0)
        {
            scale /= 10;
            v += (*p - '0') * scale;
        }
        else
            v = v * 10 + (*p - '0');
    }
    return neg ? -v : v;
}

static void set_entry(double v)
{
    format_number(v, C.entry, sizeof(C.entry));
    if (strcmp(C.entry, "Error") == 0)
        C.error = true;
}

static double apply(double a, char op, double b)
{
    switch (op)
    {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b == 0 ? NAN : a / b;
        default:  return b;
    }
}

static const char *op_symbol(char op)
{
    switch (op)
    {
        case '*': return "x";
        case '/': return "/";
        case '+': return "+";
        case '-': return "-";
        default:  return "";
    }
}

static void press(const CalcKey *k)
{
    if (C.error && k->kind != K_CLEAR)
        return;

    switch (k->kind)
    {
        case K_DIGIT:
            if (C.fresh || strcmp(C.entry, "0") == 0)
            {
                C.entry[0] = '\0';
                C.fresh = false;
            }
            if (strlen(C.entry) < 15)
            {
                size_t n = strlen(C.entry);
                C.entry[n] = k->value;
                C.entry[n + 1] = '\0';
            }
            break;

        case K_DOT:
            if (C.fresh)
            {
                strcpy(C.entry, "0");
                C.fresh = false;
            }
            if (!strchr(C.entry, '.'))
                strcat(C.entry, ".");
            break;

        case K_OP:
        {
            char buf[32];
            if (C.op && !C.fresh)
                set_entry(apply(C.acc, C.op, entry_value()));
            C.acc = entry_value();
            C.op = k->value;
            C.fresh = true;
            format_number(C.acc, buf, sizeof(buf));
            snprintf(C.history, sizeof(C.history), "%s %s", buf, op_symbol(C.op));
            break;
        }

        case K_EQ:
            if (C.op)
            {
                char a[32], b[32];
                double rhs = entry_value();
                format_number(C.acc, a, sizeof(a));
                format_number(rhs, b, sizeof(b));
                snprintf(C.history, sizeof(C.history), "%s %s %s =", a, op_symbol(C.op), b);
                set_entry(apply(C.acc, C.op, rhs));
                C.op = 0;
                C.fresh = true;
            }
            break;

        case K_CLEAR:
            strcpy(C.entry, "0");
            C.acc = 0;
            C.op = 0;
            C.fresh = true;
            C.error = false;
            C.history[0] = '\0';
            break;

        case K_SIGN:
            set_entry(-entry_value());
            break;

        case K_PERCENT:
            set_entry(C.op ? C.acc * entry_value() / 100.0 : entry_value() / 100.0);
            break;

        case K_BACK:
            if (!C.fresh)
            {
                size_t n = strlen(C.entry);
                if (n > 0)
                    C.entry[n - 1] = '\0';
                if (C.entry[0] == '\0' || strcmp(C.entry, "-") == 0)
                    strcpy(C.entry, "0");
            }
            break;
    }
}

static SxRect key_rect(const CalcKey *k)
{
    int bw = (WIN_W - GAP * (COLS + 1)) / COLS;
    int bh = (WIN_H - DISPLAY_H - GAP * (ROWS + 1)) / ROWS;
    return sx_rect(GAP + k->col * (bw + GAP), DISPLAY_H + GAP + k->row * (bh + GAP),
                   bw * k->span + GAP * (k->span - 1), bh);
}

static int key_hit(int x, int y)
{
    for (int i = 0; i < KEY_N; i++)
        if (sx_rect_contains(key_rect(&keys[i]), x, y))
            return i;
    return -1;
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(C.win);
    sx_clear(c, SX_THEME_BG);

    SxRect d = sx_rect(GAP, GAP, WIN_W - 2 * GAP, DISPLAY_H - GAP);
    sx_ui_panel(c, d, 12, SX_THEME_SUNKEN);

    int hw = sx_text_width(&sx_font_sans, C.history);
    sx_draw_text(c, &sx_font_sans, d.x + d.w - 16 - hw, d.y + 14, C.history, SX_THEME_TEXT_DIM);

    const SxFont *big = &sx_font_sans_huge;
    if (sx_text_width(big, C.entry) > d.w - 32)
        big = &sx_font_sans_large;
    int ew = sx_text_width(big, C.entry);
    sx_draw_text(c, big, d.x + d.w - 16 - ew, d.y + d.h - sx_font_line_height(big) - 6, C.entry,
                 C.error ? SX_THEME_RED : SX_RGB(255, 255, 255));

    for (int i = 0; i < KEY_N; i++)
    {
        const CalcKey *k = &keys[i];
        uint32_t base = SX_THEME_SURFACE_HI, text = SX_THEME_TEXT;
        if (k->kind == K_OP)
        {
            base = SX_RGB(0x3A, 0x33, 0x22);
            text = SX_THEME_ACCENT;
            if (C.op == k->value && C.fresh)
            {
                base = SX_THEME_ACCENT;
                text = SX_THEME_ACCENT_TEXT;
            }
        }
        else if (k->kind == K_EQ)
        {
            base = SX_THEME_ACCENT;
            text = SX_THEME_ACCENT_TEXT;
        }
        else if (k->kind == K_CLEAR || k->kind == K_SIGN || k->kind == K_PERCENT)
        {
            base = SX_THEME_BORDER;
        }

        SxButtonState st = C.pressed == i ? SX_BUTTON_PRESSED : (C.hover == i ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL);
        sx_ui_button(c, key_rect(k), k->label, &sx_font_sans_large, st, base, text);
    }

    sx_window_commit(C.win);
}

static int key_for_char(int ch, int key)
{
    if (key == SX_KEY_ENTER)
        ch = '=';
    if (key == SX_KEY_ESCAPE || key == SX_KEY_DELETE)
        return 0;
    for (int i = 0; i < KEY_N; i++)
    {
        const CalcKey *k = &keys[i];
        if ((k->kind == K_DIGIT || k->kind == K_OP) && k->value == ch)
            return i;
        if (k->kind == K_DOT && (ch == '.' || ch == ','))
            return i;
        if (k->kind == K_EQ && ch == '=')
            return i;
        if (k->kind == K_PERCENT && ch == '%')
            return i;
    }
    if (ch == 'x' || ch == 'X')
        return key_for_char('*', 0);
    return -1;
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("calc: cannot connect to the display server\n");
        return 1;
    }

    C.win = sx_window_create("Calculator", WIN_W, WIN_H, 0);
    if (!C.win)
        return 1;

    strcpy(C.entry, "0");
    C.fresh = true;
    C.hover = C.pressed = -1;
    draw();

    SxEvent ev;
    bool dirty = false;
    while (sx_next_event(&ev, -1) > 0)
    {
        bool redraw = false;
        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
            case SX_EVENT_DISCONNECTED:
                goto out;

            case SX_EVENT_RESIZE:
                redraw = true;
                break;

            case SX_EVENT_MOUSE_MOVE:
            {
                int h = key_hit(ev.x, ev.y);
                if (h != C.hover)
                {
                    C.hover = h;
                    redraw = true;
                }
                break;
            }

            case SX_EVENT_MOUSE_LEAVE:
                C.hover = -1;
                redraw = true;
                break;

            case SX_EVENT_MOUSE_DOWN:
                if (ev.button == SX_BUTTON_LEFT)
                {
                    C.pressed = key_hit(ev.x, ev.y);
                    redraw = true;
                }
                break;

            case SX_EVENT_MOUSE_UP:
                if (C.pressed >= 0 && key_hit(ev.x, ev.y) == C.pressed)
                    press(&keys[C.pressed]);
                C.pressed = -1;
                redraw = true;
                break;

            case SX_EVENT_KEY_DOWN:
            {
                if (ev.key == SX_KEY_BACKSPACE)
                {
                    static const CalcKey back = { "", K_BACK, 0, 0, 0, 1 };
                    press(&back);
                    redraw = true;
                    break;
                }
                int k = key_for_char(ev.ch, ev.key);
                if (k >= 0)
                {
                    press(&keys[k]);
                    C.pressed = k;
                    redraw = true;
                }
                break;
            }

            case SX_EVENT_KEY_UP:
                if (C.pressed >= 0)
                {
                    C.pressed = -1;
                    redraw = true;
                }
                break;

            default:
                break;
        }

        dirty |= redraw;
        if (dirty && !sx_pending())
        {
            draw();
            dirty = false;
        }
    }

out:
    sx_window_destroy(C.win);
    sx_disconnect();
    return 0;
}
