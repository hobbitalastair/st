/* See LICENSE for license details. */
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <fontconfig/fontconfig.h>
#include <fcft/fcft.h>
#include <pixman-1/pixman.h>

#include "drwl.h"
#include "bufpool.h"
#include "xdg-shell-protocol.h"

char *argv0;
#include "arg.h"
#include "st.h"
#include "win.h"

/* types for config.h */
typedef struct {
	uint mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

typedef struct {
	uint mod;
	uint button;
	void (*func)(const Arg *);
	const Arg arg;
	uint  release;
} MouseShortcut;

typedef struct {
	xkb_keysym_t k;
	uint mask;
	char *s;
	signed char appkey;
	signed char appcursor;
} Key;

#define XKB_KEY_ANY_MOD UINT_MAX
#define XKB_KEY_NO_MOD  0
#define Button1 1
#define Button2 2
#define Button3 3
#define Button4 4
#define Button5 5

/* forward declarations for functions referenced by config.h */
static void clipcopy(const Arg *);
static void clippaste(const Arg *);
static void numlock(const Arg *);
static void selpaste(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);
static void ttysend(const Arg *);
static void bpress(int, double, double, uint);
static void brelease(int, double, double, uint);
static void xhints(void);
/* static stub functions for listener callbacks */

#include "config.h"

static void
noop_seat_name(void *data, struct wl_seat *seat, const char *name) {}

static void
noop_output_name(void *data, struct wl_output *output, const char *name) {}

static void
noop_output_description(void *data, struct wl_output *output, const char *description) {}

static void
noop_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}

#define IS_SET(flag)		((win.mode & (flag)) != 0)
#define TRUERED(x)		(((x) & 0xff0000) >> 8)
#define TRUEGREEN(x)		(((x) & 0xff00))
#define TRUEBLUE(x)		(((x) & 0xff) << 8)

typedef struct {
	uint32_t *col;
	size_t collen;
	Fnt *font, *bfont, *ifont, *ibfont;
} DC;

static DC dc;

typedef struct {
	int tw, th;
	int w, h;
	int ch;
	int cw;
	int mode;
	int cursor;
} TermWindow;

static TermWindow win;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_keyboard *wl_keyboard;
static struct wl_pointer *wl_pointer;
static struct wl_data_device_manager *data_device_manager;
static struct wl_data_device *data_device;
static struct wl_data_source *clipboard_source;
static struct xdg_wm_base *wm_base;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;
static struct wl_surface *surface;
static struct wl_callback *frame_callback;
static struct wl_output *output;
static int32_t scale = 1;
static int32_t win_w, win_h;
static int frame_pending;

static Drwl *drw;
static BufPool pool;
static DrwBuf *curbuf;

typedef struct {
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	struct xkb_compose_table *compose_table;
	struct xkb_compose_state *compose_state;
	struct {
		int delay;
		int period;
		int timer;
		enum wl_keyboard_key_state key_state;
		xkb_keysym_t sym;
	} repeat;
	uint32_t serial;
	int ctrl, shift, alt;
} Kbd;

static Kbd kbd = { .repeat.timer = -1 };

typedef struct {
	char *primary, *clipboard;
	struct timespec tclick1;
	struct timespec tclick2;
} WlSelection;

typedef struct {
	struct wl_data_offer *offer;
	const char *mime;
} DataOffer;

static WlSelection wlsel;
static DataOffer *data_offer, *pending_offer;
static uint buttons;

static char *opt_class = NULL;
static char **opt_cmd  = NULL;
static char *opt_font  = NULL;
static char *opt_io    = NULL;
static char *opt_line  = NULL;
static char *opt_name  = NULL;
static char *opt_title = NULL;

static char *usedfont = NULL;
static double usedfontsize = 0;
static double defaultfontsize = 0;

static void xdrawglyphfontspecs(const Glyph *, int, int, int);
static void xdrawglyph(Glyph, int, int);
static void xclear(int, int, int, int);
static void xinit(int, int);
static void cresize(int, int);
static void xresize(int, int);
static void run(void);
static void usage(void);
static void xactivate(void);

/* color parsing */
static ushort sixd_to_16bit(int);
static void frame_handle_done(void *, struct wl_callback *, uint32_t);

static const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

static void
frame_handle_done(void *data, struct wl_callback *callback, uint32_t time)
{
	wl_callback_destroy(callback);
	if (frame_callback == callback)
		frame_callback = NULL;
	frame_pending = 0;
	draw();
}

static uint32_t
color_parse(const char *name)
{
	static const struct { const char *name; uint32_t argb; } cmap[] = {
		{ "black",   0xFF000000 }, { "red",     0xFFFF0000 },
		{ "red3",    0xFFCD0000 }, { "green",   0xFF00FF00 },
		{ "green3",  0xFF00CD00 }, { "yellow",  0xFFFFFF00 },
		{ "yellow3", 0xFFCDCD00 }, { "blue",    0xFF0000FF },
		{ "blue2",   0xFF0000EE }, { "magenta", 0xFFFF00FF },
		{ "magenta3",0xFFCD00CD }, { "cyan",    0xFF00FFFF },
		{ "cyan3",   0xFF00CDCD }, { "white",   0xFFFFFFFF },
		{ "gray90",  0xFFE5E5E5 }, { "gray50",  0xFF7F7F7F },
	};
	unsigned int i;
	if (name[0] == '#') {
		unsigned long v = strtoul(name + 1, NULL, 16);
		int len = strlen(name + 1);
		if (len == 6)
			return 0xFF000000 | (v & 0xFFFFFF);
		/* #RRGGBBAA → 0xAARRGGBB */
		return ((v & 0xFF) << 24) | ((v >> 24) & 0xFF) << 16
			| ((v >> 16) & 0xFF) << 8 | ((v >> 8) & 0xFF);
	}
	for (i = 0; i < LEN(cmap); i++)
		if (!strcmp(name, cmap[i].name))
			return cmap[i].argb;
	return 0xFF000000;
}

static uint32_t
color_for_idx(int i)
{
	if (i < LEN(colorname) && colorname[i])
		return color_parse(colorname[i]);
	if (BETWEEN(i, 16, 255)) {
		if (i < 6*6*6+16) {
			uint8_t r = ((i - 16) / 36) % 6;
			uint8_t g = ((i - 16) / 6) % 6;
			uint8_t b = ((i - 16) / 1) % 6;
			return 0xFF000000
				| (sixd_to_16bit(r) >> 8) << 16
				| (sixd_to_16bit(g) >> 8) << 8
				| (sixd_to_16bit(b) >> 8);
		} else {
			uint8_t gray = 8 + 10 * (i - (6*6*6+16));
			return 0xFF000000
				| gray << 16 | gray << 8 | gray;
		}
	}
	return 0xFF000000;
}

void
xloadcols(void)
{
	static int loaded;
	int i;
	if (loaded)
		free(dc.col);
	dc.collen = MAX(LEN(colorname), 256);
	dc.col = xmalloc(dc.collen * sizeof(uint32_t));
	for (i = 0; i < dc.collen; i++)
		dc.col[i] = color_for_idx(i);
	loaded = 1;
}

int
xgetcolor(int x, unsigned char *r, unsigned char *g, unsigned char *b)
{
	if (!BETWEEN(x, 0, (int)dc.collen - 1))
		return 1;
	*r = (dc.col[x] >> 16) & 0xFF;
	*g = (dc.col[x] >> 8) & 0xFF;
	*b = dc.col[x] & 0xFF;
	return 0;
}

int
xsetcolorname(int x, const char *name)
{
	if (!BETWEEN(x, 0, (int)dc.collen - 1))
		return 1;
	if (name == NULL) {
		dc.col[x] = color_for_idx(x);
	} else {
		dc.col[x] = color_parse(name);
	}
	return 0;
}

static uint32_t
glyph_color(uint32_t c)
{
	if (IS_TRUECOL(c)) {
		return 0xFF000000 | ((c >> 16) & 0xFF) << 16
		       | ((c >> 8) & 0xFF) << 8 | (c & 0xFF);
	}
	return dc.col[c];
}

/* font loading */
static int
xloadfont(Fnt **fntp, const char *name, const char *attributes)
{
	if (*fntp)
		fcft_destroy(*fntp);
	*fntp = fcft_from_name(1, &name, attributes);
	return *fntp ? 0 : 1;
}

static void
xloadfonts(const char *fontstr, double fontsize)
{
	int s = scale < 1 ? 1 : scale;
	double requestedfontsize = fontsize;
	FcPattern *pattern = FcNameParse((const FcChar8 *)fontstr);
	if (!pattern)
		die("can't open font %s\n", fontstr);

	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize * s);
	} else {
		double fontval;
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval)
				== FcResultMatch) {
			requestedfontsize = fontval;
			FcPatternDel(pattern, FC_PIXEL_SIZE);
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontval * s);
		}
		FcPatternDel(pattern, FC_DPI);
		FcPatternAddDouble(pattern, FC_DPI, 96.0 * s);
	}

	{
		FcChar8 *name = FcNameUnparse(pattern);
		if (xloadfont(&dc.font, (const char *)name, NULL))
			die("can't open font %s\n", fontstr);
		free(name);
	}

	if (requestedfontsize <= 1)
		requestedfontsize = (double)dc.font->height / s;
	usedfontsize = requestedfontsize;
	if (defaultfontsize <= 0)
		defaultfontsize = usedfontsize;

	{
		const struct fcft_glyph *g = fcft_rasterize_char_utf32(
			dc.font, L'W', FCFT_SUBPIXEL_DEFAULT);
		int cw = g && g->advance.x > 0
			? g->advance.x : dc.font->max_advance.x;
		win.cw = ceilf(cw * cwscale);
	}
	win.ch = ceilf(dc.font->height * chscale);

	/* Italic */
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	{
		FcChar8 *name = FcNameUnparse(pattern);
		Fnt *ifont = NULL;
		if (xloadfont(&ifont, (const char *)name, NULL))
			dc.ifont = dc.font;
		else
			dc.ifont = ifont;
		free(name);
	}

	/* Bold-italic */
	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	{
		FcChar8 *name = FcNameUnparse(pattern);
		Fnt *ibfont = NULL;
		if (xloadfont(&ibfont, (const char *)name, NULL))
			dc.ibfont = dc.font;
		else
			dc.ibfont = ibfont;
		free(name);
	}

	/* Bold */
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	{
		FcChar8 *name = FcNameUnparse(pattern);
		Fnt *bfont = NULL;
		if (xloadfont(&bfont, (const char *)name, NULL))
			dc.bfont = dc.font;
		else
			dc.bfont = bfont;
		free(name);
	}

	FcPatternDestroy(pattern);
}

static void
xunloadfonts(void)
{
	if (dc.bfont && dc.bfont != dc.font)
		fcft_destroy(dc.bfont);
	if (dc.ifont && dc.ifont != dc.font && dc.ifont != dc.bfont)
		fcft_destroy(dc.ifont);
	if (dc.ibfont && dc.ibfont != dc.font && dc.ibfont != dc.bfont && dc.ibfont != dc.ifont)
		fcft_destroy(dc.ibfont);
	if (dc.font)
		fcft_destroy(dc.font);
	dc.font = dc.bfont = dc.ifont = dc.ibfont = NULL;
}

/* pixman helpers */
static pixman_color_t
pixman_color(uint32_t argb)
{
	return (pixman_color_t){
		.red   = (((argb >> 16) & 0xFF) * 0x101),
		.green = (((argb >> 8) & 0xFF) * 0x101),
		.blue  = (((argb) & 0xFF) * 0x101),
		.alpha = (((argb >> 24) & 0xFF) * 0x101),
	};
}

void
xclear(int x1, int y1, int x2, int y2)
{
	if (!drw || !drw->image)
		return;
	uint32_t bg = IS_SET(MODE_REVERSE) ? dc.col[defaultfg] : dc.col[defaultbg];
	pixman_color_t clr = pixman_color(bg);
	pixman_image_fill_rectangles(PIXMAN_OP_SRC, drw->image, &clr, 1,
		&(pixman_rectangle16_t){x1, y1, x2 - x1, y2 - y1});
}

ushort
sixd_to_16bit(int x)
{
	return x == 0 ? 0 : 0x3737 + 0x2828 * x;
}

void
xdrawglyphfontspecs(const Glyph *glyphs, int len, int x, int y)
{
	Fnt *font;
	uint32_t fg, bg, temp;
	int i, hideglyphs;

	if (len < 1 || glyphs[0].mode == ATTR_WDUMMY)
		return;

	int charlen = len * ((glyphs[0].mode & ATTR_WIDE) ? 2 : 1);
	int winx = borderpx + x * win.cw, winy = borderpx + y * win.ch;
	int width = charlen * win.cw;

	font = dc.font;
	if ((glyphs[0].mode & ATTR_ITALIC) && (glyphs[0].mode & ATTR_BOLD))
		font = dc.ibfont;
	else if (glyphs[0].mode & ATTR_ITALIC)
		font = dc.ifont;
	else if (glyphs[0].mode & ATTR_BOLD)
		font = dc.bfont;

	fg = glyph_color(glyphs[0].fg);
	bg = glyph_color(glyphs[0].bg);

	if (IS_SET(MODE_REVERSE)) {
		temp = fg; fg = bg; bg = temp;
	}

	if ((glyphs[0].mode & ATTR_BOLD_FAINT) == ATTR_BOLD
	    && BETWEEN(glyphs[0].fg, 0, 7))
		fg = dc.col[glyphs[0].fg + 8];

	if ((glyphs[0].mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
		fg = 0xFF000000
			| ((((fg >> 16) & 0xFF) / 2) << 16)
			| ((((fg >> 8) & 0xFF) / 2) << 8)
			| ((fg & 0xFF) / 2);
	}

	if (glyphs[0].mode & ATTR_REVERSE) {
		temp = fg; fg = bg; bg = temp;
	}

	hideglyphs = (glyphs[0].mode & ATTR_INVISIBLE)
		|| ((glyphs[0].mode & ATTR_BLINK) && (win.mode & MODE_BLINK));
	if (hideglyphs)
		fg = bg;

	xclear(winx, winy, winx + width, winy + win.ch);

	pixman_color_t bgc = pixman_color(bg);
	pixman_image_fill_rectangles(PIXMAN_OP_SRC, drw->image, &bgc, 1,
		&(pixman_rectangle16_t){winx, winy, width, win.ch});

	pixman_color_t fgc = pixman_color(fg);
	pixman_image_t *fgp = pixman_image_create_solid_fill(&fgc);

	int xp = winx;
	int yp = winy + (win.ch - font->height) / 2 + font->ascent;

	for (i = 0; !hideglyphs && i < len; i++) {
		Rune rune = glyphs[i].u;
		ushort mode = glyphs[i].mode;
		if (rune == 0 || mode == ATTR_WDUMMY)
			continue;

		const struct fcft_glyph *g = fcft_rasterize_char_utf32(
			font, rune, FCFT_SUBPIXEL_DEFAULT);
		if (!g)
			continue;

		if (g->is_color_glyph)
			pixman_image_composite32(PIXMAN_OP_OVER,
				g->pix, NULL, drw->image,
				0, 0, 0, 0,
				xp + g->x, yp - g->y,
				g->width, g->height);
		else
			pixman_image_composite32(PIXMAN_OP_OVER,
				fgp, g->pix, drw->image,
				0, 0, 0, 0,
				xp + g->x, yp - g->y,
				g->width, g->height);

		xp += win.cw * ((mode & ATTR_WIDE) ? 2 : 1);
	}

	pixman_image_unref(fgp);

	if (glyphs[0].mode & ATTR_UNDERLINE) {
		pixman_color_t uc = pixman_color(fg);
		pixman_image_fill_rectangles(PIXMAN_OP_SRC, drw->image, &uc, 1,
			&(pixman_rectangle16_t){winx,
				winy + font->ascent + 1, width, 1});
	}
	if (glyphs[0].mode & ATTR_STRUCK) {
		pixman_color_t uc = pixman_color(fg);
		pixman_image_fill_rectangles(PIXMAN_OP_SRC, drw->image, &uc, 1,
			&(pixman_rectangle16_t){winx,
				winy + 2 * font->ascent / 3, width, 1});
	}
}

void
xdrawglyph(Glyph g, int x, int y)
{
	xdrawglyphfontspecs(&g, 1, x, y);
}

int
xstartdraw(void)
{
	if (!IS_SET(MODE_VISIBLE))
		return 0;
	if (frame_pending)
		return 0;
	curbuf = bufpool_getbuf(&pool, shm, win_w, win_h);
	if (!curbuf)
		return 0;
	drwl_setimage(drw, curbuf->image);
	return 1;
}

void
xdrawline(Line line, int x1, int y1, int x2)
{
	int i = 0, x, ox = x1;
	Glyph base, batch[256];
	int batch_max = LEN(batch);

	for (x = x1; x < x2; x++) {
		Glyph g = line[x];
		if (g.mode == ATTR_WDUMMY)
			continue;
		if (selected(x, y1))
			g.mode ^= ATTR_REVERSE;
		if (i > 0 && ATTRCMP(base, g)) {
			xdrawglyphfontspecs(batch, i, ox, y1);
			i = 0;
		}
		if (i == batch_max) {
			xdrawglyphfontspecs(batch, i, ox, y1);
			i = 0;
		}
		if (i == 0) {
			ox = x;
			base = g;
		}
		batch[i++] = g;
	}
	if (i > 0)
		xdrawglyphfontspecs(batch, i, ox, y1);
}

void
xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	if (selected(ox, oy))
		og.mode ^= ATTR_REVERSE;
	xdrawglyph(og, ox, oy);

	if (IS_SET(MODE_HIDE))
		return;

	g.mode &= ATTR_BOLD|ATTR_ITALIC|ATTR_UNDERLINE|ATTR_STRUCK|ATTR_WIDE;

	uint32_t drawcol;
	if (IS_SET(MODE_REVERSE)) {
		g.mode |= ATTR_REVERSE;
		g.bg = defaultfg;
		if (selected(cx, cy)) {
			drawcol = dc.col[defaultcs];
			g.fg = defaultrcs;
		} else {
			drawcol = dc.col[defaultrcs];
			g.fg = defaultcs;
		}
	} else {
		if (selected(cx, cy)) {
			g.fg = defaultfg;
			g.bg = defaultrcs;
		} else {
			g.fg = defaultbg;
			g.bg = defaultcs;
		}
		drawcol = dc.col[g.bg];
	}

	pixman_color_t pc = pixman_color(drawcol);

	if (IS_SET(MODE_FOCUSED)) {
		switch (win.cursor) {
		case 7:
			g.u = 0x2603;
		case 0: case 1: case 2:
			xdrawglyph(g, cx, cy);
			break;
		case 3: case 4:
			pixman_image_fill_rectangles(PIXMAN_OP_SRC,
				drw->image, &pc, 1,
				&(pixman_rectangle16_t){
					borderpx + cx * win.cw,
					borderpx + (cy + 1) * win.ch - cursorthickness,
					win.cw, cursorthickness});
			break;
		case 5: case 6:
			pixman_image_fill_rectangles(PIXMAN_OP_SRC,
				drw->image, &pc, 1,
				&(pixman_rectangle16_t){
					borderpx + cx * win.cw,
					borderpx + cy * win.ch,
					cursorthickness, win.ch});
			break;
		}
	} else {
		pixman_image_fill_rectangles(PIXMAN_OP_SRC, drw->image, &pc, 4,
			(pixman_rectangle16_t[4]){
				{ borderpx + cx * win.cw, borderpx + cy * win.ch, win.cw - 1, 1 },
				{ borderpx + cx * win.cw, borderpx + cy * win.ch, 1, win.ch - 1 },
				{ borderpx + (cx + 1) * win.cw - 1, borderpx + cy * win.ch, 1, win.ch - 1 },
				{ borderpx + cx * win.cw, borderpx + (cy + 1) * win.ch - 1, win.cw, 1 }});
	}
}

void
xfinishdraw(void)
{
	if (!curbuf)
		return;
	drwl_setimage(drw, NULL);
	frame_callback = wl_surface_frame(surface);
	wl_callback_add_listener(frame_callback, &frame_listener, NULL);
	frame_pending = 1;
	wl_surface_set_buffer_scale(surface, scale);
	wl_surface_attach(surface, curbuf->wl_buf, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, win_w, win_h);
	wl_surface_commit(surface);
	curbuf = NULL;
}

/* window */
void
cresize(int width, int height)
{
	int col, row;
	if (width != 0)
		win.w = width;
	if (height != 0)
		win.h = height;
	col = (win.w - 2 * borderpx) / win.cw;
	row = (win.h - 2 * borderpx) / win.ch;
	col = MAX(1, col);
	row = MAX(1, row);
	tresize(col, row);
	xresize(col, row);
	ttyresize(win.tw, win.th);
}

void
xresize(int col, int row)
{
	win.tw = col * win.cw;
	win.th = row * win.ch;
	win_w = win.w;
	win_h = win.h;
}

void
xsetenv(void)
{
	setenv("WINDOWID", "0", 1);
}

void
xseticontitle(char *p)
{
}

void
xsettitle(char *p)
{
	DEFAULT(p, opt_title);
	if (!p)
		return;
	if (p[0] == '\0')
		p = opt_title;
	if (xdg_toplevel)
		xdg_toplevel_set_title(xdg_toplevel, p);
}

void
xsetmode(int set, unsigned int flags)
{
	int mode = win.mode;
	MODBIT(win.mode, set, flags);
	if ((win.mode & (MODE_REVERSE|MODE_HIDE)) !=
	    (mode & (MODE_REVERSE|MODE_HIDE)))
		redraw();
}

int
xsetcursor(int cursor)
{
	if (!BETWEEN(cursor, 0, 7))
		return 1;
	win.cursor = cursor;
	return 0;
}

void
xsetpointermotion(int set)
{
}

void
xbell(void)
{
	if (bellvolume)
		fprintf(stderr, "\a");
}

void
xclipcopy(void)
{
	free(wlsel.clipboard);
	wlsel.clipboard = wlsel.primary ? strdup(wlsel.primary) : NULL;
	if (wlsel.clipboard)
		clipcopy(NULL);
}

void
xsetsel(char *str)
{
	free(wlsel.primary);
	wlsel.primary = str;
}

/* input */
static int
evcol(double x)
{
	int c = (x - borderpx) / win.cw;
	LIMIT(c, 0, win.tw / win.cw - 1);
	return c;
}

static int
evrow(double y)
{
	int r = (y - borderpx) / win.ch;
	LIMIT(r, 0, win.th / win.ch - 1);
	return r;
}

static int
match(uint mask, uint state)
{
	return mask == XKB_KEY_ANY_MOD || mask == (state & ~ignoremod);
}

static char *
kmap(xkb_keysym_t k, uint state)
{
	Key *kp;
	int i;
	for (i = 0; i < LEN(mappedkeys); i++) {
		if (mappedkeys[i] == k)
			break;
	}
	if (i == LEN(mappedkeys)) {
		if ((k & 0xFFFF) < 0xFD00)
			return NULL;
	}
	for (kp = key; kp < key + LEN(key); kp++) {
		if (kp->k != k)
			continue;
		if (!match(kp->mask, state))
			continue;
		if (IS_SET(MODE_APPKEYPAD) ? kp->appkey < 0 : kp->appkey > 0)
			continue;
		if (IS_SET(MODE_NUMLOCK) && kp->appkey == 2)
			continue;
		if (IS_SET(MODE_APPCURSOR) ? kp->appcursor < 0 : kp->appcursor > 0)
			continue;
		return kp->s;
	}
	return NULL;
}

static void
mousesel(int done, double x, double y, uint mods)
{
	int type, seltype = SEL_REGULAR;
	for (type = 1; type < LEN(selmasks); ++type) {
		if (match(selmasks[type], mods)) {
			seltype = type;
			break;
		}
	}
	selextend(evcol(x), evrow(y), seltype, done);
}

static void
mousereport(int button, int action, double x, double y, uint state)
{
	int len, btn, code, cx = evcol(x), cy = evrow(y);
	char buf[40];
	static int ox, oy;

	btn = button;
	if (action == 2) {
		if (cx == ox && cy == oy)
			return;
		if (!IS_SET(MODE_MOUSEMOTION) && !IS_SET(MODE_MOUSEMANY))
			return;
		if (IS_SET(MODE_MOUSEMOTION) && buttons == 0)
			return;
		for (btn = 1; btn <= 11 && !(buttons & (1<<(btn-1))); btn++)
			;
		code = 32;
	} else {
		if (btn < 1 || btn > 11)
			return;
		if (action == 1) {
			if (IS_SET(MODE_MOUSEX10))
				return;
			if (btn == 4 || btn == 5)
				return;
		}
		code = 0;
	}
	ox = cx;
	oy = cy;

	if ((!IS_SET(MODE_MOUSESGR) && action == 1) || btn == 12)
		code += 3;
	else if (btn >= 8)
		code += 128 + btn - 8;
	else if (btn >= 4)
		code += 64 + btn - 4;
	else
		code += btn - 1;

	if (!IS_SET(MODE_MOUSEX10)) {
		code += ((state & 1) ? 4 : 0)
		      + ((state & 8) ? 8 : 0)
		      + ((state & 4) ? 16 : 0);
	}

	if (IS_SET(MODE_MOUSESGR)) {
		len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
			code, cx + 1, cy + 1, action == 1 ? 'm' : 'M');
	} else if (cx < 223 && cy < 223) {
		len = snprintf(buf, sizeof(buf), "\033[M%c%c%c",
			32 + code, 32 + cx + 1, 32 + cy + 1);
	} else
		return;
	ttywrite(buf, len, 0);
}

/* keyboard */
void
kpress(xkb_keysym_t ksym, uint state)
{
	char buf[64], *customkey;
	int len;
	Rune c;
	Shortcut *bp;

	if (IS_SET(MODE_KBDLOCK))
		return;

	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if (ksym == bp->keysym && match(bp->mod, state)) {
			bp->func(&(bp->arg));
			return;
		}
	}

	if ((customkey = kmap(ksym, state))) {
		ttywrite(customkey, strlen(customkey), 1);
		return;
	}

	len = xkb_keysym_to_utf8(ksym, buf, sizeof buf);
	if (len <= 0)
		return;
	len--;  /* xkb_keysym_to_utf8 includes the NUL terminator in its count */

	if (len == 1 && (state & 4)) {
		if (*buf >= 'a' && *buf <= 'z')
			*buf -= 'a' - 1;
		else if (*buf >= 'A' && *buf <= 'Z')
			*buf -= 'A' - 1;
	}

	if (len == 1 && (state & 8)) {
		if (IS_SET(MODE_8BIT)) {
			if (*buf < 0177) {
				c = *buf | 0x80;
				len = utf8encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
	}
	ttywrite(buf, len, 1);
}

/* keyboard listeners */
static void
keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size)
{
	char *map_shm;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
		die("unknown keymap");
	map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map_shm == MAP_FAILED)
		die("mmap:");
	keymap = xkb_keymap_new_from_string(kbd.context, map_shm,
		XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_shm, size);
	close(fd);
	if (!keymap)
		die("xkb_keymap_new_from_string failed");
	state = xkb_state_new(keymap);
	if (!state)
		die("xkb_state_new failed");
	if (kbd.state)
		xkb_state_unref(kbd.state);
	if (kbd.keymap)
		xkb_keymap_unref(kbd.keymap);
	kbd.keymap = keymap;
	kbd.state = state;
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface,
		struct wl_array *keys)
{
	win.mode |= MODE_FOCUSED;
	tdirtycursor();
	if (IS_SET(MODE_FOCUS) && IS_SET(MODE_VISIBLE))
		ttywrite("\033[I", 3, 0);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface)
{
	struct itimerspec spec = { 0 };
	win.mode &= ~MODE_FOCUSED;
	tdirtycursor();
	if (IS_SET(MODE_FOCUS) && IS_SET(MODE_VISIBLE))
		ttywrite("\033[O", 3, 0);
	if (kbd.repeat.timer >= 0)
		timerfd_settime(kbd.repeat.timer, 0, &spec, NULL);
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state)
{
	struct itimerspec spec = { 0 };
	enum wl_keyboard_key_state key_state = _key_state;
	kbd.serial = serial;
	if (!kbd.state)
		return;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(kbd.state, key + 8);
	uint mods = 0;

	if (kbd.ctrl)  mods |= 4;
	if (kbd.alt)   mods |= 8;
	if (kbd.shift) mods |= 1;

	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && kbd.compose_state) {
		switch (xkb_compose_state_feed(kbd.compose_state, sym)) {
		case XKB_COMPOSE_FEED_ACCEPTED:
			switch (xkb_compose_state_get_status(kbd.compose_state)) {
			case XKB_COMPOSE_COMPOSED:
				sym = xkb_compose_state_get_one_sym(kbd.compose_state);
				xkb_compose_state_reset(kbd.compose_state);
				break;
			case XKB_COMPOSE_COMPOSING:
				kbd.repeat.key_state = WL_KEYBOARD_KEY_STATE_RELEASED;
				if (kbd.repeat.timer >= 0)
					timerfd_settime(kbd.repeat.timer, 0, &spec, NULL);
				return;
			case XKB_COMPOSE_CANCELLED:
				xkb_compose_state_reset(kbd.compose_state);
				kbd.repeat.key_state = WL_KEYBOARD_KEY_STATE_RELEASED;
				if (kbd.repeat.timer >= 0)
					timerfd_settime(kbd.repeat.timer, 0, &spec, NULL);
				return;
			case XKB_COMPOSE_NOTHING:
				break;
			}
			break;
		case XKB_COMPOSE_FEED_IGNORED:
			break;
		}
	}

	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED)
		kpress(sym, mods);

	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && kbd.repeat.period >= 0
			&& kbd.repeat.delay > 0
			&& xkb_keymap_key_repeats(kbd.keymap, key + 8)) {
		kbd.repeat.key_state = key_state;
		kbd.repeat.sym = sym;
		spec.it_value.tv_sec = kbd.repeat.delay / 1000;
		spec.it_value.tv_nsec = (kbd.repeat.delay % 1000) * 1000000l;
	} else {
		kbd.repeat.key_state = key_state;
	}
	if (kbd.repeat.timer >= 0)
		timerfd_settime(kbd.repeat.timer, 0, &spec, NULL);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
	if (!kbd.state)
		return;
	xkb_state_update_mask(kbd.state,
		mods_depressed, mods_latched, mods_locked, group, 0, 0);
	kbd.ctrl = xkb_state_mod_name_is_active(kbd.state,
		XKB_MOD_NAME_CTRL,
		XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
	kbd.shift = xkb_state_mod_name_is_active(kbd.state,
		XKB_MOD_NAME_SHIFT,
		XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
	kbd.alt = xkb_state_mod_name_is_active(kbd.state,
		XKB_MOD_NAME_ALT,
		XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
}

static void
keyboard_repeat(void)
{
	struct itimerspec spec = { 0 };
	uint mods = 0;

	if (kbd.repeat.key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (kbd.ctrl)  mods |= 4;
	if (kbd.alt)   mods |= 8;
	if (kbd.shift) mods |= 1;
	kpress(kbd.repeat.sym, mods);

	if (kbd.repeat.period <= 0)
		return;
	spec.it_value.tv_sec = kbd.repeat.period / 1000;
	spec.it_value.tv_nsec = (kbd.repeat.period % 1000) * 1000000l;
	if (kbd.repeat.timer >= 0)
		timerfd_settime(kbd.repeat.timer, 0, &spec, NULL);
}

static void
keyboard_handle_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay)
{
	kbd.repeat.delay = delay;
	kbd.repeat.period = rate > 0 ? 1000 / rate : -1;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info,
};

/* mouse */
static int
wl_button_to_logical(uint32_t b)
{
	switch (b) {
	case BTN_LEFT:   return 1;
	case BTN_RIGHT:  return 3;
	case BTN_MIDDLE: return 2;
	case BTN_SIDE:   return 8;
	case BTN_EXTRA:  return 9;
	default:         return 0;
	}
}

static double pointer_x, pointer_y;
static uint pointer_mods;

static void
pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	pointer_x = wl_fixed_to_double(surface_x);
	pointer_y = wl_fixed_to_double(surface_y);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface)
{
}

static void
pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	pointer_x = wl_fixed_to_double(surface_x);
	pointer_y = wl_fixed_to_double(surface_y);
	if (IS_SET(MODE_MOUSE)) {
		mousereport(0, 2, pointer_x, pointer_y, 0);
		return;
	}
	mousesel(0, pointer_x, pointer_y, pointer_mods);
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button,
		uint32_t state)
{
	int btn = wl_button_to_logical(button);
	if (btn < 1 || btn > 11)
		return;

	uint mods = 0;
	if (kbd.ctrl)  mods |= 4;
	if (kbd.alt)   mods |= 8;
	if (kbd.shift) mods |= 1;
	pointer_mods = mods;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		buttons |= 1 << (btn - 1);
		if (IS_SET(MODE_MOUSE)) {
			mousereport(btn, 0, pointer_x, pointer_y, mods);
			return;
		}
		bpress(btn, pointer_x, pointer_y, mods);
	} else {
		buttons &= ~(1 << (btn - 1));
		if (IS_SET(MODE_MOUSE)) {
			mousereport(btn, 1, pointer_x, pointer_y, mods);
			return;
		}
		brelease(btn, pointer_x, pointer_y, mods);
	}
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value)
{
	int btn = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		? (value < 0 ? 4 : 5) : (value < 0 ? 6 : 7);
	int count = abs(wl_fixed_to_int(value));
	uint mods = 0;
	if (kbd.ctrl)  mods |= 4;
	if (kbd.alt)   mods |= 8;
	if (kbd.shift) mods |= 1;
	pointer_mods = mods;

	for (int i = 0; i < count && i < 3; i++) {
		if (IS_SET(MODE_MOUSE)) {
			mousereport(btn, 0, pointer_x, pointer_y, mods);
			mousereport(btn, 1, pointer_x, pointer_y, mods);
		} else if (btn <= 5) {
			bpress(btn, pointer_x, pointer_y, mods);
			brelease(btn, pointer_x, pointer_y, mods);
		}
	}
}

static void
pointer_handle_frame(void *data, struct wl_pointer *wl_pointer)
{
}

static void
pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source)
{
}

static void
pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis)
{
}

static void
pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete)
{
}

static void
pointer_handle_axis_value120(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t value120)
{
}

static void
pointer_handle_axis_relative_direction(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis,
		uint32_t direction)
{
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
	.axis_source = pointer_handle_axis_source,
	.axis_stop = pointer_handle_axis_stop,
	.axis_discrete = pointer_handle_axis_discrete,
	.axis_value120 = pointer_handle_axis_value120,
	.axis_relative_direction = pointer_handle_axis_relative_direction,
};

/* clipboard */
static void
clipboard_write(int fd, const char *data, size_t len)
{
	struct sigaction oldact, ignore = { .sa_handler = SIG_IGN };
	ssize_t n;

	sigemptyset(&ignore.sa_mask);
	sigaction(SIGPIPE, &ignore, &oldact);
	while (len > 0) {
		n = write(fd, data, len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		data += n;
		len -= n;
	}
	sigaction(SIGPIPE, &oldact, NULL);
}

static void
clipboard_source_send(void *data, struct wl_data_source *source,
		const char *mime_type, int32_t fd)
{
	if (wlsel.clipboard)
		clipboard_write(fd, wlsel.clipboard, strlen(wlsel.clipboard));
	close(fd);
}

static void
clipboard_source_cancelled(void *data, struct wl_data_source *source)
{
	wl_data_source_destroy(source);
	clipboard_source = NULL;
}

static const struct wl_data_source_listener clipboard_source_listener = {
	.send = clipboard_source_send,
	.cancelled = clipboard_source_cancelled,
};

static void
data_offer_destroy(DataOffer *offer)
{
	if (!offer)
		return;
	if (offer->offer)
		wl_data_offer_destroy(offer->offer);
	free(offer);
}

static void
data_offer_handle_offer(void *data, struct wl_data_offer *offer,
		const char *mime_type)
{
	DataOffer *wl_offer = data;

	if (!strcmp(mime_type, "text/plain;charset=utf-8"))
		wl_offer->mime = "text/plain;charset=utf-8";
	else if (!wl_offer->mime && !strcmp(mime_type, "text/plain"))
		wl_offer->mime = "text/plain";
}

static const struct wl_data_offer_listener data_offer_listener = {
	.offer = data_offer_handle_offer,
};

static void
data_device_handle_data_offer(void *data, struct wl_data_device *wl_data_device,
		struct wl_data_offer *id)
{
	data_offer_destroy(pending_offer);
	pending_offer = xmalloc(sizeof(*pending_offer));
	*pending_offer = (DataOffer){ .offer = id };
	wl_data_offer_add_listener(id, &data_offer_listener, pending_offer);
}

static void
data_device_handle_enter(void *data, struct wl_data_device *wl_data_device,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *id)
{
}

static void
data_device_handle_leave(void *data, struct wl_data_device *wl_data_device)
{
}

static void
data_device_handle_motion(void *data, struct wl_data_device *wl_data_device,
		uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
}

static void
data_device_handle_drop(void *data, struct wl_data_device *wl_data_device)
{
}

static void
data_device_handle_selection(void *data, struct wl_data_device *data_device,
		struct wl_data_offer *offer)
{
	data_offer_destroy(data_offer);
	data_offer = NULL;
	if (!offer)
		return;
	if (pending_offer && pending_offer->offer == offer) {
		data_offer = pending_offer;
		pending_offer = NULL;
	} else {
		data_offer = xmalloc(sizeof(*data_offer));
		*data_offer = (DataOffer){ .offer = offer };
		wl_data_offer_add_listener(offer, &data_offer_listener, data_offer);
	}
}

static const struct wl_data_device_listener data_device_listener = {
	.data_offer = data_device_handle_data_offer,
	.enter = data_device_handle_enter,
	.leave = data_device_handle_leave,
	.motion = data_device_handle_motion,
	.drop = data_device_handle_drop,
	.selection = data_device_handle_selection,
};

static void
ensure_data_device(void)
{
	if (!data_device_manager || !seat || data_device)
		return;
	data_device = wl_data_device_manager_get_data_device(
		data_device_manager, seat);
	wl_data_device_add_listener(data_device, &data_device_listener, NULL);
}

static void
clippaste(const Arg *dummy)
{
	int fds[2];
	ssize_t n;
	char buf[4096];

	if (!data_offer || !data_offer->mime)
		return;
	if (clipboard_source && wlsel.clipboard) {
		ttywrite(wlsel.clipboard, strlen(wlsel.clipboard), 1);
		return;
	}
	if (pipe(fds) < 0)
		die("pipe");
	wl_data_offer_receive(data_offer->offer, data_offer->mime, fds[1]);
	close(fds[1]);
	wl_display_roundtrip(display);
	for (;;) {
		n = read(fds[0], buf, sizeof(buf));
		if (n <= 0)
			break;
		ttywrite(buf, n, 1);
	}
	close(fds[0]);
}

static void
selpaste(const Arg *dummy)
{
	clippaste(dummy);
}

static void
clipcopy(const Arg *dummy)
{
	if (!data_device_manager || !data_device || !wlsel.clipboard)
		return;

	if (clipboard_source)
		wl_data_source_destroy(clipboard_source);

	clipboard_source = wl_data_device_manager_create_data_source(data_device_manager);
	wl_data_source_offer(clipboard_source, "text/plain;charset=utf-8");
	wl_data_source_add_listener(clipboard_source, &clipboard_source_listener, NULL);
	wl_data_device_set_selection(data_device, clipboard_source, kbd.serial);
	wl_display_roundtrip(display);
}

void
numlock(const Arg *dummy)
{
	win.mode ^= MODE_NUMLOCK;
}

void
zoom(const Arg *arg)
{
	Arg larg;
	larg.f = usedfontsize + arg->f;
	zoomabs(&larg);
}

void
zoomabs(const Arg *arg)
{
	xunloadfonts();
	xloadfonts(usedfont, arg->f);
	cresize(0, 0);
	redraw();
	xhints();
}

void
zoomreset(const Arg *arg)
{
	Arg larg;
	if (defaultfontsize > 0) {
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

void
ttysend(const Arg *arg)
{
	ttywrite(arg->s, strlen(arg->s), 1);
}

/* Wayland listeners */
static void
xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
		uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states)
{
	if (width > 0 || height > 0)
		cresize(width * scale, height * scale);
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	ttyhangup();
	exit(0);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

static void
wl_output_handle_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t phys_w, int32_t phys_h,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform)
{
}

static void
wl_output_handle_mode(void *data, struct wl_output *wl_output,
		uint32_t flags, int32_t width, int32_t height,
		int32_t refresh)
{
}

static void
wl_output_handle_done(void *data, struct wl_output *wl_output)
{
}

static void
wl_output_handle_scale(void *data, struct wl_output *wl_output,
		int32_t factor)
{
	scale = factor > 0 ? factor : 1;
}

static const struct wl_output_listener output_listener = {
	.geometry = wl_output_handle_geometry,
	.mode = wl_output_handle_mode,
	.done = wl_output_handle_done,
	.scale = wl_output_handle_scale,
	.name = noop_output_name,
	.description = noop_output_description,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps)
{
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl_keyboard) {
		wl_keyboard = wl_seat_get_keyboard(seat);
		if (!kbd.context)
			kbd.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!kbd.context)
			die("xkb_context_new failed");
		if (!kbd.compose_table)
			kbd.compose_table = xkb_compose_table_new_from_locale(kbd.context,
				setlocale(LC_CTYPE, NULL), XKB_COMPOSE_COMPILE_NO_FLAGS);
		if (kbd.compose_table && !kbd.compose_state)
			kbd.compose_state = xkb_compose_state_new(kbd.compose_table,
				XKB_COMPOSE_STATE_NO_FLAGS);
		if ((kbd.repeat.timer = timerfd_create(CLOCK_MONOTONIC, 0)) < 0)
			die("timerfd_create:");
		wl_keyboard_add_listener(wl_keyboard, &keyboard_listener, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl_keyboard) {
		struct itimerspec spec = { 0 };
		if (kbd.repeat.timer >= 0) {
			timerfd_settime(kbd.repeat.timer, 0, &spec, NULL);
			close(kbd.repeat.timer);
			kbd.repeat.timer = -1;
		}
		if (kbd.state)
			xkb_state_unref(kbd.state);
		if (kbd.keymap)
			xkb_keymap_unref(kbd.keymap);
		if (kbd.compose_state)
			xkb_compose_state_unref(kbd.compose_state);
		if (kbd.compose_table)
			xkb_compose_table_unref(kbd.compose_table);
		if (kbd.context)
			xkb_context_unref(kbd.context);
		kbd.state = NULL;
		kbd.keymap = NULL;
		kbd.compose_state = NULL;
		kbd.compose_table = NULL;
		kbd.context = NULL;
		wl_keyboard_release(wl_keyboard);
		wl_keyboard = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl_pointer) {
		wl_pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(wl_pointer, &pointer_listener, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wl_pointer) {
		wl_pointer_release(wl_pointer);
		wl_pointer = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = noop_seat_name,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	if (!strcmp(interface, wl_compositor_interface.name))
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, MIN(version, 5));
	else if (!strcmp(interface, wl_shm_interface.name))
		shm = wl_registry_bind(registry, name,
			&wl_shm_interface, 1);
	else if (!strcmp(interface, xdg_wm_base_interface.name))
		wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, 1);
	else if (!strcmp(interface, wl_seat_interface.name)) {
		seat = wl_registry_bind(registry, name,
			&wl_seat_interface, MIN(version, 7));
		wl_seat_add_listener(seat, &seat_listener, NULL);
		ensure_data_device();
	}
	else if (!strcmp(interface, wl_data_device_manager_interface.name)) {
		data_device_manager = wl_registry_bind(registry, name,
			&wl_data_device_manager_interface, MIN(version, 3));
		ensure_data_device();
	}
	else if (!strcmp(interface, wl_output_interface.name)) {
		output = wl_registry_bind(registry, name,
			&wl_output_interface, MIN(version, 4));
		wl_output_add_listener(output, &output_listener, NULL);
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = noop_global_remove,
};

static void
xhints(void)
{
	int s = scale < 1 ? 1 : scale;
	if (xdg_toplevel) {
		xdg_toplevel_set_min_size(xdg_toplevel,
			DIVCEIL(win.cw + 2 * borderpx, s),
			DIVCEIL(win.ch + 2 * borderpx, s));
	}
}

void
xinit(int cols, int rows)
{
	if (!(display = wl_display_connect(NULL)))
		die("failed to connect to Wayland display\n");

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!compositor) die("wl_compositor not available\n");
	if (!shm)        die("wl_shm not available\n");
	if (!wm_base)    die("xdg_wm_base not available\n");

	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

	usedfont = (opt_font == NULL) ? font : opt_font;
	drwl_init();
	xloadfonts(usedfont, 0);
	xloadcols();

	win.w = 2 * borderpx + cols * win.cw;
	win.h = 2 * borderpx + rows * win.ch;
	xresize(cols, rows);

	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
	xdg_toplevel_set_title(xdg_toplevel, opt_title ? opt_title : "st");
	xdg_toplevel_set_app_id(xdg_toplevel, "st");

	wl_surface_commit(surface);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display);

	if (!(drw = drwl_create()))
		die("cannot create drwl drawing context");

	win.mode = MODE_NUMLOCK;
	resettitle();
	xhints();

	wl_display_roundtrip(display);
	wl_display_roundtrip(display);

	xactivate();

	ensure_data_device();

	clock_gettime(CLOCK_MONOTONIC, &wlsel.tclick1);
	clock_gettime(CLOCK_MONOTONIC, &wlsel.tclick2);
	wlsel.primary = NULL;
	wlsel.clipboard = NULL;
}

/* mouse actions */
static int
mouseaction(int btn, uint release, uint mods)
{
	MouseShortcut *ms;
	for (ms = mshortcuts; ms < mshortcuts + LEN(mshortcuts); ms++) {
		if (ms->release == release &&
		    ms->button == btn &&
		    (match(ms->mod, mods) ||
		     match(ms->mod, mods & ~forcemousemod))) {
			ms->func(&(ms->arg));
			return 1;
		}
	}
	return 0;
}

void
bpress(int btn, double x, double y, uint mods)
{
	struct timespec now;
	int snap;

	if (1 <= btn && btn <= 11)
		buttons |= 1 << (btn - 1);

	if (IS_SET(MODE_MOUSE)) {
		mousereport(btn, 0, x, y, mods);
		return;
	}
	if (mouseaction(btn, 0, mods))
		return;
	if (btn == 1) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (TIMEDIFF(now, wlsel.tclick2) <= tripleclicktimeout)
			snap = SNAP_LINE;
		else if (TIMEDIFF(now, wlsel.tclick1) <= doubleclicktimeout)
			snap = SNAP_WORD;
		else
			snap = 0;
		wlsel.tclick2 = wlsel.tclick1;
		wlsel.tclick1 = now;
		selstart(evcol(x), evrow(y), snap);
	}
}

void
brelease(int btn, double x, double y, uint mods)
{
	char *sel;

	if (1 <= btn && btn <= 11)
		buttons &= ~(1 << (btn - 1));
	if (IS_SET(MODE_MOUSE)) {
		mousereport(btn, 1, x, y, mods);
		return;
	}
	if (mouseaction(btn, 1, mods))
		return;
	if (btn == 1) {
		mousesel(1, x, y, mods);
		if ((sel = getsel())) {
			xsetsel(sel);
			xclipcopy();
		}
	}
}

void
bmotion(double x, double y, uint btns)
{
	if (IS_SET(MODE_MOUSE)) {
		mousereport(0, 2, x, y, 0);
		return;
	}
	mousesel(0, x, y, pointer_mods);
}

/* event loop */
void
run(void)
{
	fd_set rfd;
	int wayland_fd = wl_display_get_fd(display), ttyfd, wayland_ev, drawing,
	    maxfd, prepared_read;
	struct timespec seltv, *tv, now, lastblink, trigger;
	double timeout;
	uint64_t repeatexp;

	ttyfd = ttynew(opt_line, shell, opt_io, opt_cmd);
	win.mode |= MODE_VISIBLE;

	for (timeout = -1, drawing = 0, lastblink = (struct timespec){0};;) {
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(wayland_fd, &rfd);
		maxfd = MAX(wayland_fd, ttyfd);
		if (kbd.repeat.timer >= 0) {
			FD_SET(kbd.repeat.timer, &rfd);
			maxfd = MAX(maxfd, kbd.repeat.timer);
		}

		wayland_ev = 0;
		prepared_read = 0;
		if (wl_display_prepare_read(display) < 0) {
			if (wl_display_dispatch_pending(display) < 0)
				die("wl_display_dispatch_pending:");
			wayland_ev = 1;
			wayland_fd = wl_display_get_fd(display);
			FD_SET(wayland_fd, &rfd);
		} else
			prepared_read = 1;

		if (wl_display_flush(display) < 0)
			die("wl_display_flush:");

		seltv.tv_sec = timeout / 1E3;
		seltv.tv_nsec = 1E6 * (timeout - 1E3 * seltv.tv_sec);
		tv = timeout >= 0 ? &seltv : NULL;

		if (pselect(maxfd + 1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (prepared_read)
				wl_display_cancel_read(display);
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		clock_gettime(CLOCK_MONOTONIC, &now);

		if (prepared_read && FD_ISSET(wayland_fd, &rfd)) {
			if (wl_display_read_events(display) < 0)
				die("wl_display_read_events:");
			if (wl_display_dispatch_pending(display) < 0)
				die("wl_display_dispatch_pending:");
			wayland_ev = 1;
		} else if (prepared_read) {
			wl_display_cancel_read(display);
		} else if (FD_ISSET(wayland_fd, &rfd)) {
			if (wl_display_dispatch(display) < 0)
				die("wl_display_dispatch:");
			wayland_ev = 1;
		}

		if (FD_ISSET(ttyfd, &rfd))
			ttyread();

		if (kbd.repeat.timer >= 0 && FD_ISSET(kbd.repeat.timer, &rfd)) {
			if (read(kbd.repeat.timer, &repeatexp, sizeof(repeatexp)) > 0)
				keyboard_repeat();
		}

		if (FD_ISSET(ttyfd, &rfd) || wayland_ev) {
			if (!drawing) {
				trigger = now;
				drawing = 1;
			}
			timeout = (maxlatency - TIMEDIFF(now, trigger))
			          / maxlatency * minlatency;
			if (timeout > 0)
				continue;
		}

		timeout = -1;
		if (blinktimeout && tattrset(ATTR_BLINK)) {
			timeout = blinktimeout - TIMEDIFF(now, lastblink);
			if (timeout <= 0) {
				if (-timeout > blinktimeout)
					win.mode |= MODE_BLINK;
				win.mode ^= MODE_BLINK;
				tsetdirtattr(ATTR_BLINK);
				lastblink = now;
				timeout = blinktimeout;
			}
		}

		draw();
		drawing = 0;
	}
}

static void
activation_token_handle_done(void *data,
		struct xdg_activation_token_v1 *token, const char *token_str)
{
	*(char **)data = xstrdup(token_str);
}

static const struct xdg_activation_token_v1_listener activation_token_listener = {
	.done = activation_token_handle_done,
};

const char *
xgetactivationtoken(const char *app_id)
{
	char *token_str = NULL;

	if (!activation_manager || !surface)
		return NULL;

	struct xdg_activation_token_v1 *token =
		xdg_activation_v1_get_activation_token(activation_manager);

	xdg_activation_token_v1_set_surface(token, surface);

	if (app_id && app_id[0])
		xdg_activation_token_v1_set_app_id(token, app_id);

	if (kbd.serial)
		xdg_activation_token_v1_set_serial(token, kbd.serial, seat);

	xdg_activation_token_v1_add_listener(token,
		&activation_token_listener, &token_str);
	xdg_activation_token_v1_commit(token);

	wl_display_roundtrip(display);

	xdg_activation_token_v1_destroy(token);

	return token_str;
}

static void
xactivate(void)
{
	const char *token;

	if (!activation_manager || !surface)
		return;

	token = getenv("XDG_ACTIVATION_TOKEN");
	if (token && token[0]) {
		xdg_activation_v1_activate(activation_manager, token, surface);
		wl_display_flush(display);
	}
	unsetenv("XDG_ACTIVATION_TOKEN");
}

void
usage(void)
{
	die("usage: %s [-aiv] [-c class] [-f font] [-g geometry]"
	    " [-n name] [-o file]\n"
	    "          [-T title] [-t title]"
	    " [[-e] command [args ...]]\n"
	    "       %s [-aiv] [-c class] [-f font] [-g geometry]"
	    " [-n name] [-o file]\n"
	    "          [-T title] [-t title] -l line"
	    " [stty_args ...]\n", argv0, argv0);
}

int
main(int argc, char *argv[])
{
	xsetcursor(cursorshape);

	ARGBEGIN {
	case 'a':
		allowaltscreen = 0;
		break;
	case 'c':
		opt_class = EARGF(usage());
		break;
	case 'e':
		if (argc > 0)
			--argc, ++argv;
		goto run;
	case 'f':
		opt_font = EARGF(usage());
		break;
	case 'g':
		EARGF(usage());
		break;
	case 'i':
		break;
	case 'o':
		opt_io = EARGF(usage());
		break;
	case 'l':
		opt_line = EARGF(usage());
		break;
	case 'n':
		opt_name = EARGF(usage());
		break;
	case 't': case 'T':
		opt_title = EARGF(usage());
		break;
	case 'v':
		die("%s " VERSION "\n", argv0);
		break;
	default:
		usage();
	} ARGEND;

run:
	if (argc > 0)
		opt_cmd = argv;
	if (!opt_title)
		opt_title = (opt_line || !opt_cmd) ? "st" : opt_cmd[0];

	setlocale(LC_CTYPE, "");
	cols = MAX(cols, 1);
	rows = MAX(rows, 1);
	tnew(cols, rows);
	xinit(cols, rows);
	xsetenv();
	selinit();
	run();

	return 0;
}
