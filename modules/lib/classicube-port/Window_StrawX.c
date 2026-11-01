/* ClassiCube window backend for StrawX: one StrawX window shared by the launcher and the
   game, the software renderer's framebuffer copied into it, and StrawX input translated
   into ClassiCube's. Mouse look uses the StrawX pointer grab. */
#include "_WindowBase.h"
#include "String_.h"
#include "Funcs.h"
#include "Bitmap.h"
#include "Options.h"
#include "Errors.h"
#include "Utils.h"

#include <stdlib.h>
#include <strawx.h>
#include <ui.h>

static SxWindow* win;

/* the StrawX connection drops when the display server goes away: treat it as a close */
static void HandleClose(void) {
	if (!Window_Main.Exists) return;
	Window_Main.Exists = false;
	Event_RaiseVoid(&WindowEvents.Closing);
}


/*########################################################################################################################*
*-----------------------------------------------------------Keys----------------------------------------------------------*
*#########################################################################################################################*/
static int MapNativeKey(int key) {
	if (key >= 'a' && key <= 'z') return key - 'a' + 'A';
	if (key >= '0' && key <= '9') return key;

	switch (key) {
	case SX_KEY_BACKSPACE: return CCKEY_BACKSPACE;
	case SX_KEY_TAB:       return CCKEY_TAB;
	case SX_KEY_ENTER:     return CCKEY_ENTER;
	case SX_KEY_ESCAPE:    return CCKEY_ESCAPE;
	case SX_KEY_SPACE:     return CCKEY_SPACE;
	case SX_KEY_DELETE:    return CCKEY_DELETE;

	case '`':  return CCKEY_TILDE;
	case '-':  return CCKEY_MINUS;
	case '=':  return CCKEY_EQUALS;
	case '[':  return CCKEY_LBRACKET;
	case ']':  return CCKEY_RBRACKET;
	case '/':  return CCKEY_SLASH;
	case ';':  return CCKEY_SEMICOLON;
	case '\'': return CCKEY_QUOTE;
	case ',':  return CCKEY_COMMA;
	case '.':  return CCKEY_PERIOD;
	case '\\': return CCKEY_BACKSLASH;
	case '*':  return CCKEY_KP_MULTIPLY;
	case '+':  return CCKEY_KP_PLUS;

	case SX_KEY_UP:        return CCKEY_UP;
	case SX_KEY_DOWN:      return CCKEY_DOWN;
	case SX_KEY_LEFT:      return CCKEY_LEFT;
	case SX_KEY_RIGHT:     return CCKEY_RIGHT;
	case SX_KEY_HOME:      return CCKEY_HOME;
	case SX_KEY_END:       return CCKEY_END;
	case SX_KEY_PAGE_UP:   return CCKEY_PAGEUP;
	case SX_KEY_PAGE_DOWN: return CCKEY_PAGEDOWN;
	case SX_KEY_INSERT:    return CCKEY_INSERT;
	case SX_KEY_LSHIFT:    return CCKEY_LSHIFT;
	case SX_KEY_RSHIFT:    return CCKEY_RSHIFT;
	case SX_KEY_LCTRL:     return CCKEY_LCTRL;
	case SX_KEY_RCTRL:     return CCKEY_RCTRL;
	case SX_KEY_LALT:      return CCKEY_LALT;
	case SX_KEY_RALT:      return CCKEY_RALT;
	case SX_KEY_LSUPER:    return CCKEY_LWIN;
	case SX_KEY_RSUPER:    return CCKEY_RWIN;
	case SX_KEY_CAPS_LOCK: return CCKEY_CAPSLOCK;
	case SX_KEY_MENU:      return CCKEY_MENU;
	}

	if (key >= SX_KEY_F1 && key <= SX_KEY_F12) return CCKEY_F1 + (key - SX_KEY_F1);
	return INPUT_NONE;
}

static int MapNativeButton(int button) {
	switch (button) {
	case SX_BUTTON_LEFT:   return CCMOUSE_L;
	case SX_BUTTON_RIGHT:  return CCMOUSE_R;
	case SX_BUTTON_MIDDLE: return CCMOUSE_M;
	}
	return INPUT_NONE;
}


/*########################################################################################################################*
*--------------------------------------------------Public implementation--------------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) { }

/* StrawOS defaults, applied only while the user hasn't chosen something else (options are
   loaded by the time Window_Init runs). ClassiCube's built-in font list has no StrawOS font,
   so the font defaults to the system Noto Sans. */
static void SetDefaultOption(const char* key, const char* value) {
	cc_string cur; char curBuffer[STRING_SIZE];
	cc_string val = String_FromReadonly(value);

	String_InitArray(cur, curBuffer);
	Options_Get(key, &cur, "");
	if (!cur.length) Options_Set(key, &val);
}

void Window_Init(void) {
	if (sx_connect() != 0) Process_Abort("Cannot connect to the StrawX display server");

	DisplayInfo.Width  = sx_screen_width();
	DisplayInfo.Height = sx_screen_height();
	DisplayInfo.Depth  = 32;
	DisplayInfo.ScaleX = 1.0f;
	DisplayInfo.ScaleY = 1.0f;
	DisplayInfo.CursorVisible = true;

	SetDefaultOption(OPT_FONT_NAME, "Noto Sans");
}

void Window_Free(void) {
	if (win) sx_window_destroy(win);
	win = NULL;
	sx_disconnect();
}

/* the launcher and the game take turns with the same window, which is resized between them.
   The new size is assumed straight away so the renderer allocates for it; if the server
   settles on another size, its resize event corrects it. */
static void DoCreateWindow(int width, int height) {
	if (!win) {
		win = sx_window_create(GAME_APP_TITLE, width, height, SX_WINDOW_RESIZABLE);
		if (!win) Process_Abort("Cannot create a StrawX window");
		width  = sx_window_width(win);
		height = sx_window_height(win);
	} else if (width != sx_window_width(win) || height != sx_window_height(win)) {
		sx_window_resize(win, width, height);
	}

	Window_Main.Width      = width;
	Window_Main.Height     = height;
	Window_Main.Exists     = true;
	Window_Main.Focused    = true;
	Window_Main.Handle.ptr = win;
	Window_Main.UIScaleX   = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY   = DEFAULT_UI_SCALE_Y;
}

void Window_Create2D(int width, int height) { DoCreateWindow(width, height); Window_Main.Is3D = false; }
void Window_Create3D(int width, int height) { DoCreateWindow(width, height); Window_Main.Is3D = true;  }

/* kept open for whichever of launcher or game runs next; Window_Free closes it */
void Window_Destroy(void) {
	if (win) sx_window_grab_pointer(win, false);
}

void Window_SetTitle(const cc_string* title) {
	char str[NATIVE_STR_LEN];
	String_EncodeUtf8(str, title);
	if (win) sx_window_set_title(win, str);
}

void Clipboard_GetText(cc_string* value) {
	size_t len;
	char* text = sx_clipboard_get(&len);
	if (!text) return;

	String_AppendUtf8(value, text, (int)len);
	free(text);
}

void Clipboard_SetText(const cc_string* value) {
	char str[NATIVE_STR_LEN];
	int len = String_EncodeUtf8(str, value);
	sx_clipboard_set(str, (size_t)len);
}

int Window_GetWindowState(void) { return WINDOW_STATE_NORMAL; }

cc_result Window_EnterFullscreen(void) { return ERR_NOT_SUPPORTED; }
cc_result Window_ExitFullscreen(void)  { return ERR_NOT_SUPPORTED; }

int Window_IsObscured(void) { return 0; }

/* StrawX maps a window when its first frame arrives */
void Window_Show(void) { }

void Window_SetSize(int width, int height) {
	if (win) sx_window_resize(win, width, height);
}

void Window_RequestClose(void) {
	HandleClose();
}

static void OnKey(const SxEvent* ev, cc_bool pressed) {
	int key = MapNativeKey(ev->key);
	if (key) Input_Set(key, pressed);

	/* typed characters: StrawX delivers ASCII alongside the key */
	if (pressed && ev->ch >= 32 && ev->ch < 127 && !(ev->mods & (SX_MOD_CTRL | SX_MOD_ALT)))
		Event_RaiseInt(&InputEvents.Press, ev->ch);
}

/* events for the game window that arrived while a dialog was up, replayed afterwards */
#define DEFERRED_MAX 64
static SxEvent deferred[DEFERRED_MAX];
static int deferred_count;

static void HandleEvent(const SxEvent* e) {
	SxEvent ev = *e;
	switch (ev.type) {
	case SX_EVENT_MOUSE_MOVE:
	case SX_EVENT_MOUSE_ENTER:
		Pointer_SetPosition(0, ev.x, ev.y);
		break;

	case SX_EVENT_MOUSE_RELATIVE:
		if (Input.RawMode) Event_RaiseRawMove(&PointerEvents.RawMoved, (float)ev.x, (float)ev.y);
		break;

	case SX_EVENT_MOUSE_DOWN:
	case SX_EVENT_MOUSE_UP:
	{
		int btn = MapNativeButton(ev.button);
		if (!Input.RawMode) Pointer_SetPosition(0, ev.x, ev.y);
		if (btn) Input_Set(btn, ev.type == SX_EVENT_MOUSE_DOWN);
		break;
	}

	case SX_EVENT_MOUSE_WHEEL:
		Mouse_ScrollVWheel((float)ev.wheel);
		break;

	case SX_EVENT_KEY_DOWN: OnKey(&ev, true);  break;
	case SX_EVENT_KEY_UP:   OnKey(&ev, false); break;

	case SX_EVENT_FOCUS_IN:
	case SX_EVENT_FOCUS_OUT:
		Window_Main.Focused = ev.type == SX_EVENT_FOCUS_IN;
		Event_RaiseVoid(&WindowEvents.FocusChanged);
		break;

	case SX_EVENT_RESIZE:
		/* also sent when the theme changes, so always repaint */
		if (ev.width != Window_Main.Width || ev.height != Window_Main.Height) {
			Window_Main.Width  = ev.width;
			Window_Main.Height = ev.height;
			Event_RaiseVoid(&WindowEvents.Resized);
		}
		Event_RaiseVoid(&WindowEvents.RedrawNeeded);
		break;

	case SX_EVENT_CLOSE:
	case SX_EVENT_DISCONNECTED:
		HandleClose();
		break;

	default:
		break;
	}
}

void Window_ProcessEvents(float delta) {
	SxEvent ev;
	int i, r;

	for (i = 0; i < deferred_count; i++) HandleEvent(&deferred[i]);
	deferred_count = 0;

	while ((r = sx_next_event(&ev, 0)) > 0) HandleEvent(&ev);
	if (r < 0) HandleClose();
}

/* StrawX cannot warp the cursor; mouse look uses the pointer grab instead */
void Cursor_SetPosition(int x, int y) { }
static void Cursor_GetRawPos(int* x, int* y) { *x = 0; *y = 0; }
static void Cursor_DoSetVisible(cc_bool visible) { }

void Window_EnableRawMouse(void) {
	Input.RawMode = true;
	if (win) sx_window_grab_pointer(win, true);
}

void Window_UpdateRawMouse(void) { }

void Window_DisableRawMouse(void) {
	Input.RawMode = false;
	if (win) sx_window_grab_pointer(win, false);
}

void Gamepads_PreInit(void) { }
void Gamepads_Init(void) { }
void Gamepads_Process(float delta) { }

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) { return ERR_NOT_SUPPORTED; }
cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) { return ERR_NOT_SUPPORTED; }


/*########################################################################################################################*
*---------------------------------------------------------Dialogs---------------------------------------------------------*
*#########################################################################################################################*/
#define DIALOG_W   420
#define DIALOG_PAD 16
#define DIALOG_MAX_LINES 24

/* wraps msg into lines of at most width pixels; returns the line count */
static int WrapLines(const char* msg, int width, const char** starts, int* lens) {
	int count = 0;
	const char* p = msg;

	while (*p && count < DIALOG_MAX_LINES) {
		int len = 0, fit = 0;
		while (p[len] && p[len] != '\n') {
			len++;
			if (sx_text_width_n(&sx_font_sans, p, len) > width) break;
			if (p[len] == ' ' || p[len] == '\0' || p[len] == '\n') fit = len;
		}
		if (p[len] && p[len] != '\n' && fit > 0) len = fit;
		else if (p[len] && p[len] != '\n') len = len > 1 ? len - 1 : 1;

		starts[count] = p;
		lens[count]   = len;
		count++;

		p += len;
		while (*p == ' ') p++;
		if (*p == '\n') p++;
	}
	return count;
}

/* a small modal window: ClassiCube uses dialogs for errors it cannot show in-game */
static void ShowDialogCore(const char* title, const char* msg) {
	const char* starts[DIALOG_MAX_LINES];
	int lens[DIALOG_MAX_LINES];
	int count, line_h, h, i, y;
	cc_string titleStr;
	SxWindow* dlg;
	SxCanvas* c;
	SxEvent ev;

	Platform_LogConst(title);
	Platform_LogConst(msg);
	if (sx_fd() < 0) return;
	/* the font cache lives in the RAM filesystem, so this notice would come up on every boot;
	   scanning /share/fonts is quick anyway */
	titleStr = String_FromReadonly(title);
	if (String_CaselessEqualsConst(&titleStr, "One time load")) return;

	count  = WrapLines(msg, DIALOG_W - DIALOG_PAD * 2, starts, lens);
	line_h = sx_font_sans.line_height + 4;
	h      = DIALOG_PAD * 3 + count * line_h + line_h;

	dlg = sx_window_create(title, DIALOG_W, h, 0);
	if (!dlg) return;

	c = sx_window_canvas(dlg);
	sx_clear(c, SX_THEME_BG);
	for (i = 0, y = DIALOG_PAD; i < count; i++, y += line_h)
		sx_draw_text_n(c, &sx_font_sans, DIALOG_PAD, y, starts[i], lens[i], SX_THEME_TEXT);
	sx_draw_text(c, &sx_font_sans_small, DIALOG_PAD, h - DIALOG_PAD - line_h,
		"Press Enter or close this window to continue", SX_THEME_TEXT_DIM);
	sx_window_commit(dlg);

	for (;;) {
		if (sx_next_event(&ev, -1) < 0) break;
		if (ev.window != dlg) {
			/* keep what matters (resizes, closing, releases) for the next event pump */
			if (deferred_count < DEFERRED_MAX && ev.type != SX_EVENT_MOUSE_MOVE &&
				ev.type != SX_EVENT_MOUSE_RELATIVE) deferred[deferred_count++] = ev;
			continue;
		}
		if (ev.type == SX_EVENT_CLOSE) break;
		if (ev.type == SX_EVENT_KEY_DOWN && (ev.key == SX_KEY_ENTER || ev.key == SX_KEY_ESCAPE)) break;
	}
	sx_window_destroy(dlg);
}


/*########################################################################################################################*
*-------------------------------------------------------Framebuffer-------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0  = (BitmapCol*)Mem_Alloc(width * height, BITMAPCOLOR_SIZE, "window pixels");
	bmp->width  = width;
	bmp->height = height;
}

/* ClassiCube's BitmapCol is 0xAARRGGBB on x86, the same as a StrawX canvas; only alpha is
   forced, since the software renderer leaves it undefined */
void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	SxCanvas* c;
	int x, y, x2, y2;
	if (!win) return;

	c  = sx_window_canvas(win);
	x2 = min(r.x + r.width,  min(bmp->width,  c->width));
	y2 = min(r.y + r.height, min(bmp->height, c->height));
	if (r.x < 0) r.x = 0;
	if (r.y < 0) r.y = 0;
	if (x2 <= r.x || y2 <= r.y) return;

	for (y = r.y; y < y2; y++) {
		const BitmapCol* src = Bitmap_GetRow(bmp, y);
		uint32_t* dst = c->pixels + (size_t)y * c->stride;
		for (x = r.x; x < x2; x++) dst[x] = src[x] | 0xFF000000u;
	}
	sx_window_damage(win, r.x, r.y, x2 - r.x, y2 - r.y);
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
}
