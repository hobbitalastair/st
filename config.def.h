/* See LICENSE file for copyright and license details. */

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static char *font = "Liberation Mono:pixelsize=12:antialias=true:autohint=true";
static int borderpx = 2;

/*
 * What program is execed by st depends of these precedence rules:
 * 1: program passed with -e
 * 2: scroll and/or utmp
 * 3: SHELL environment variable
 * 4: value of shell in /etc/passwd
 * 5: value of shell in config.h
 */
static char *shell = "/bin/sh";
char *utmp = NULL;
/* scroll program: to enable use a string like "scroll" */
char *scroll = NULL;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";

/* identification sequence returned in DA and DECID */
char *vtiden = "\033[?6c";

/* Kerning / character bounding-box multipliers */
static float cwscale = 1.0;
static float chscale = 1.0;

/*
 * word delimiter string
 *
 * More advanced example: L" `'\"()[]{}"
 */
wchar_t *worddelimiters = L" ";

/* selection timeouts (in milliseconds) */
static unsigned int doubleclicktimeout = 300;
static unsigned int tripleclicktimeout = 600;

/* alt screens */
int allowaltscreen = 1;

/* allow certain non-interactive (insecure) window operations such as:
   setting the clipboard text */
int allowwindowops = 0;

/*
 * draw latency range in ms - from new content/keypress/etc until drawing.
 * within this range, st draws when content stops arriving (idle). mostly it's
 * near minlatency, but it waits longer for slow updates to avoid partial draw.
 * low minlatency will tear/flicker more, as it can "detect" idle too early.
 */
static double minlatency = 2;
static double maxlatency = 33;

/*
 * blinking timeout (set to 0 to disable blinking) for the terminal blinking
 * attribute.
 */
static unsigned int blinktimeout = 800;

/*
 * thickness of underline and bar cursors
 */
static unsigned int cursorthickness = 2;

/*
 * bell volume. It must be a value between -100 and 100. Use 0 for disabling
 * it
 */
static int bellvolume = 0;

/* default TERM value */
char *termname = "st-256color";

/*
 * spaces per tab
 *
 * When you are changing this value, don't forget to adapt the »it« value in
 * the st.info and appropriately install the st.info in the environment where
 * you use this st version.
 *
 *	it#$tabspaces,
 *
 * Secondly make sure your kernel is not expanding tabs. When running `stty
 * -a` »tab0« should appear. You can tell the terminal to not expand tabs by
 *  running following command:
 *
 *	stty tabs
 */
unsigned int tabspaces = 8;

/* Terminal colors (16 first used in escape sequence) */
static const char *colorname[] = {
	/* 8 normal colors */
	"black",
	"red3",
	"green3",
	"yellow3",
	"blue2",
	"magenta3",
	"cyan3",
	"gray90",

	/* 8 bright colors */
	"gray50",
	"red",
	"green",
	"yellow",
	"#5c5cff",
	"magenta",
	"cyan",
	"white",

	[255] = 0,

	/* more colors can be added after 255 to use with DefaultXX */
	"#cccccc",
	"#555555",
	"gray90", /* default foreground colour */
	"black", /* default background colour */
};


/*
 * Default colors (colorname index)
 * foreground, background, cursor, reverse cursor
 */
unsigned int defaultfg = 258;
unsigned int defaultbg = 259;
unsigned int defaultcs = 256;
static unsigned int defaultrcs = 257;

/*
 * Default shape of cursor
 * 2: Block ("█")
 * 4: Underline ("_")
 * 6: Bar ("|")
 * 7: Snowman ("☃")
 */
static unsigned int cursorshape = 2;

/*
 * Default columns and rows numbers
 */

static unsigned int cols = 80;
static unsigned int rows = 24;

/*
 * Default colour and shape of the mouse cursor
 */
static unsigned int mousefg = 7;
static unsigned int mousebg = 0;

/*
 * Color used to display font attributes when fontconfig selected a font which
 * doesn't match the ones requested.
 */
static unsigned int defaultattr = 11;

/*
 * Force mouse select/shortcuts while mask is active (when MODE_MOUSE is set).
 * Note that if you want to use MOD_SHIFT with selmasks, set this to an other
 * modifier, set to 0 to not use it.
 */

/*
 * Modifier masks — these must match the bit positions built in
 * keyboard_handle_key() in wayland.c.
 *
 * XKB_KEY_Meta_L etc. are KeySym constants (identifying physical keys),
 * NOT modifier masks.  They cannot be used here.
 */
#define MOD_SHIFT 1
#define MOD_CTRL  4
#define MOD_ALT   8
#define MOD_MOD3  32
#define MOD_MOD4  64
#define MOD_MOD2  16

static uint forcemousemod = MOD_SHIFT;

/*
 * Internal mouse shortcuts.
 * Beware that overloading Button1 will disable the selection.
 */
static MouseShortcut mshortcuts[] = {
	/* mask                 button   function        argument       release */
	{ UINT_MAX,             2, selpaste,       {.i = 0},      1 },
	{ MOD_SHIFT,            4, ttysend,        {.s = "\033[5;2~"} },
	{ UINT_MAX,             4, ttysend,        {.s = "\031"} },
	{ MOD_SHIFT,            5, ttysend,        {.s = "\033[6;2~"} },
	{ UINT_MAX,             5, ttysend,        {.s = "\005"} },
};

/* Internal keyboard shortcuts. */
#define MODKEY MOD_ALT
#define TERMMOD (MOD_CTRL | MOD_SHIFT)

static Shortcut shortcuts[] = {
	/* mask                 keysym          function        argument */
	{ XKB_KEY_ANY_MOD,           XKB_KEY_Break,       sendbreak,      {.i =  0} },
	{ MOD_CTRL,              XKB_KEY_Print,       toggleprinter,  {.i =  0} },
	{ MOD_SHIFT,             XKB_KEY_Print,       printscreen,    {.i =  0} },
	{ XKB_KEY_ANY_MOD,           XKB_KEY_Print,       printsel,       {.i =  0} },
	{ TERMMOD,              XKB_KEY_Prior,       zoom,           {.f = +1} },
	{ TERMMOD,              XKB_KEY_Next,        zoom,           {.f = -1} },
	{ TERMMOD,              XKB_KEY_Home,        zoomreset,      {.f =  0} },
	{ TERMMOD,              XKB_KEY_C,           clipcopy,       {.i =  0} },
	{ TERMMOD,              XKB_KEY_V,           clippaste,      {.i =  0} },
	{ TERMMOD,              XKB_KEY_Y,           selpaste,       {.i =  0} },
	{ MOD_SHIFT,        XKB_KEY_Insert,      selpaste,       {.i =  0} },
	{ TERMMOD,              XKB_KEY_Num_Lock,    numlock,        {.i =  0} },
};

/*
 * Special keys (change & recompile st.info accordingly)
 *
 * Mask value:
 * * Use XKB_KEY_ANY_MOD to match the key no matter modifiers state
 * * Use XKB_KEY_NO_MOD to match the key alone (no modifiers)
 * appkey value:
 * * 0: no value
 * * > 0: keypad application mode enabled
 * *   = 2: term.numlock = 1
 * * < 0: keypad application mode disabled
 * appcursor value:
 * * 0: no value
 * * > 0: cursor application mode enabled
 * * < 0: cursor application mode disabled
 *
 * Be careful with the order of the definitions because st searches in
 * this table sequentially, so any XKB_KEY_ANY_MOD must be in the last
 * position for a key.
 */

/*
 * If you want keys other than the standard function keys (0xFD00 - 0xFFFF)
 * to be mapped below, add them to this array.
 */
static uint32_t mappedkeys[] = { -1 };

/*
 * State bits to ignore when matching key or button events.  By default,
 * numlock (Mod2Mask) and keyboard layout (XKB_KEY_SWITCH_MOD) are ignored.
 */
static uint ignoremod = MOD_MOD2; /* Mod2Mask (NumLock) */

/*
 * This is the huge key array which defines all compatibility to the Linux
 * world. Please decide about changes wisely.
 */
static Key key[] = {
	/* keysym           mask            string      appkey appcursor */
	{ XKB_KEY_KP_Home, MOD_SHIFT,      "\033[2J",       0,   -1},
	{ XKB_KEY_KP_Home, MOD_SHIFT,      "\033[1;2H",     0,   +1},
	{ XKB_KEY_KP_Home,       XKB_KEY_ANY_MOD,     "\033[H",        0,   -1},
	{ XKB_KEY_KP_Home,       XKB_KEY_ANY_MOD,     "\033[1~",       0,   +1},
	{ XKB_KEY_KP_Up,         XKB_KEY_ANY_MOD,     "\033Ox",       +1,    0},
	{ XKB_KEY_KP_Up,         XKB_KEY_ANY_MOD,     "\033[A",        0,   -1},
	{ XKB_KEY_KP_Up,         XKB_KEY_ANY_MOD,     "\033OA",        0,   +1},
	{ XKB_KEY_KP_Down,       XKB_KEY_ANY_MOD,     "\033Or",       +1,    0},
	{ XKB_KEY_KP_Down,       XKB_KEY_ANY_MOD,     "\033[B",        0,   -1},
	{ XKB_KEY_KP_Down,       XKB_KEY_ANY_MOD,     "\033OB",        0,   +1},
	{ XKB_KEY_KP_Left,       XKB_KEY_ANY_MOD,     "\033Ot",       +1,    0},
	{ XKB_KEY_KP_Left,       XKB_KEY_ANY_MOD,     "\033[D",        0,   -1},
	{ XKB_KEY_KP_Left,       XKB_KEY_ANY_MOD,     "\033OD",        0,   +1},
	{ XKB_KEY_KP_Right,      XKB_KEY_ANY_MOD,     "\033Ov",       +1,    0},
	{ XKB_KEY_KP_Right,      XKB_KEY_ANY_MOD,     "\033[C",        0,   -1},
	{ XKB_KEY_KP_Right,      XKB_KEY_ANY_MOD,     "\033OC",        0,   +1},
	{ XKB_KEY_KP_Prior, MOD_SHIFT,      "\033[5;2~",     0,    0},
	{ XKB_KEY_KP_Prior,      XKB_KEY_ANY_MOD,     "\033[5~",       0,    0},
	{ XKB_KEY_KP_Begin,      XKB_KEY_ANY_MOD,     "\033[E",        0,    0},
	{ XKB_KEY_KP_End, MOD_CTRL,    "\033[J",       -1,    0},
	{ XKB_KEY_KP_End, MOD_CTRL,    "\033[1;5F",    +1,    0},
	{ XKB_KEY_KP_End, MOD_SHIFT,      "\033[K",       -1,    0},
	{ XKB_KEY_KP_End, MOD_SHIFT,      "\033[1;2F",    +1,    0},
	{ XKB_KEY_KP_End,        XKB_KEY_ANY_MOD,     "\033[4~",       0,    0},
	{ XKB_KEY_KP_Next, MOD_SHIFT,      "\033[6;2~",     0,    0},
	{ XKB_KEY_KP_Next,       XKB_KEY_ANY_MOD,     "\033[6~",       0,    0},
	{ XKB_KEY_KP_Insert, MOD_SHIFT,      "\033[2;2~",    +1,    0},
	{ XKB_KEY_KP_Insert, MOD_SHIFT,      "\033[4l",      -1,    0},
	{ XKB_KEY_KP_Insert, MOD_CTRL,    "\033[L",       -1,    0},
	{ XKB_KEY_KP_Insert, MOD_CTRL,    "\033[2;5~",    +1,    0},
	{ XKB_KEY_KP_Insert,     XKB_KEY_ANY_MOD,     "\033[4h",      -1,    0},
	{ XKB_KEY_KP_Insert,     XKB_KEY_ANY_MOD,     "\033[2~",      +1,    0},
	{ XKB_KEY_KP_Delete, MOD_CTRL,    "\033[M",       -1,    0},
	{ XKB_KEY_KP_Delete, MOD_CTRL,    "\033[3;5~",    +1,    0},
	{ XKB_KEY_KP_Delete, MOD_SHIFT,      "\033[2K",      -1,    0},
	{ XKB_KEY_KP_Delete, MOD_SHIFT,      "\033[3;2~",    +1,    0},
	{ XKB_KEY_KP_Delete,     XKB_KEY_ANY_MOD,     "\033[P",       -1,    0},
	{ XKB_KEY_KP_Delete,     XKB_KEY_ANY_MOD,     "\033[3~",      +1,    0},
	{ XKB_KEY_KP_Multiply,   XKB_KEY_ANY_MOD,     "\033Oj",       +2,    0},
	{ XKB_KEY_KP_Add,        XKB_KEY_ANY_MOD,     "\033Ok",       +2,    0},
	{ XKB_KEY_KP_Enter,      XKB_KEY_ANY_MOD,     "\033OM",       +2,    0},
	{ XKB_KEY_KP_Enter,      XKB_KEY_ANY_MOD,     "\r",           -1,    0},
	{ XKB_KEY_KP_Subtract,   XKB_KEY_ANY_MOD,     "\033Om",       +2,    0},
	{ XKB_KEY_KP_Decimal,    XKB_KEY_ANY_MOD,     "\033On",       +2,    0},
	{ XKB_KEY_KP_Divide,     XKB_KEY_ANY_MOD,     "\033Oo",       +2,    0},
	{ XKB_KEY_KP_0,          XKB_KEY_ANY_MOD,     "\033Op",       +2,    0},
	{ XKB_KEY_KP_1,          XKB_KEY_ANY_MOD,     "\033Oq",       +2,    0},
	{ XKB_KEY_KP_2,          XKB_KEY_ANY_MOD,     "\033Or",       +2,    0},
	{ XKB_KEY_KP_3,          XKB_KEY_ANY_MOD,     "\033Os",       +2,    0},
	{ XKB_KEY_KP_4,          XKB_KEY_ANY_MOD,     "\033Ot",       +2,    0},
	{ XKB_KEY_KP_5,          XKB_KEY_ANY_MOD,     "\033Ou",       +2,    0},
	{ XKB_KEY_KP_6,          XKB_KEY_ANY_MOD,     "\033Ov",       +2,    0},
	{ XKB_KEY_KP_7,          XKB_KEY_ANY_MOD,     "\033Ow",       +2,    0},
	{ XKB_KEY_KP_8,          XKB_KEY_ANY_MOD,     "\033Ox",       +2,    0},
	{ XKB_KEY_KP_9,          XKB_KEY_ANY_MOD,     "\033Oy",       +2,    0},
	{ XKB_KEY_Up, MOD_SHIFT,      "\033[1;2A",     0,    0},
	{ XKB_KEY_Up, MOD_ALT,       "\033[1;3A",     0,    0},
	{ XKB_KEY_Up, MOD_SHIFT | MOD_ALT,"\033[1;4A",     0,    0},
	{ XKB_KEY_Up, MOD_CTRL,    "\033[1;5A",     0,    0},
	{ XKB_KEY_Up, MOD_SHIFT | MOD_CTRL,"\033[1;6A",     0,    0},
	{ XKB_KEY_Up, MOD_CTRL | MOD_ALT,"\033[1;7A",     0,    0},
	{ XKB_KEY_Up, MOD_SHIFT | MOD_CTRL | MOD_ALT,"\033[1;8A",  0,    0},
	{ XKB_KEY_Up,            XKB_KEY_ANY_MOD,     "\033[A",        0,   -1},
	{ XKB_KEY_Up,            XKB_KEY_ANY_MOD,     "\033OA",        0,   +1},
	{ XKB_KEY_Down, MOD_SHIFT,      "\033[1;2B",     0,    0},
	{ XKB_KEY_Down, MOD_ALT,       "\033[1;3B",     0,    0},
	{ XKB_KEY_Down, MOD_SHIFT | MOD_ALT,"\033[1;4B",     0,    0},
	{ XKB_KEY_Down, MOD_CTRL,    "\033[1;5B",     0,    0},
	{ XKB_KEY_Down, MOD_SHIFT | MOD_CTRL,"\033[1;6B",     0,    0},
	{ XKB_KEY_Down, MOD_CTRL | MOD_ALT,"\033[1;7B",     0,    0},
	{ XKB_KEY_Down, MOD_SHIFT | MOD_CTRL | MOD_ALT,"\033[1;8B",0,    0},
	{ XKB_KEY_Down,          XKB_KEY_ANY_MOD,     "\033[B",        0,   -1},
	{ XKB_KEY_Down,          XKB_KEY_ANY_MOD,     "\033OB",        0,   +1},
	{ XKB_KEY_Left, MOD_SHIFT,      "\033[1;2D",     0,    0},
	{ XKB_KEY_Left, MOD_ALT,       "\033[1;3D",     0,    0},
	{ XKB_KEY_Left, MOD_SHIFT | MOD_ALT,"\033[1;4D",     0,    0},
	{ XKB_KEY_Left, MOD_CTRL,    "\033[1;5D",     0,    0},
	{ XKB_KEY_Left, MOD_SHIFT | MOD_CTRL,"\033[1;6D",     0,    0},
	{ XKB_KEY_Left, MOD_CTRL | MOD_ALT,"\033[1;7D",     0,    0},
	{ XKB_KEY_Left, MOD_SHIFT | MOD_CTRL | MOD_ALT,"\033[1;8D",0,    0},
	{ XKB_KEY_Left,          XKB_KEY_ANY_MOD,     "\033[D",        0,   -1},
	{ XKB_KEY_Left,          XKB_KEY_ANY_MOD,     "\033OD",        0,   +1},
	{ XKB_KEY_Right, MOD_SHIFT,      "\033[1;2C",     0,    0},
	{ XKB_KEY_Right, MOD_ALT,       "\033[1;3C",     0,    0},
	{ XKB_KEY_Right, MOD_SHIFT | MOD_ALT,"\033[1;4C",     0,    0},
	{ XKB_KEY_Right, MOD_CTRL,    "\033[1;5C",     0,    0},
	{ XKB_KEY_Right, MOD_SHIFT | MOD_CTRL,"\033[1;6C",     0,    0},
	{ XKB_KEY_Right, MOD_CTRL | MOD_ALT,"\033[1;7C",     0,    0},
	{ XKB_KEY_Right, MOD_SHIFT | MOD_CTRL | MOD_ALT,"\033[1;8C",0,   0},
	{ XKB_KEY_Right,         XKB_KEY_ANY_MOD,     "\033[C",        0,   -1},
	{ XKB_KEY_Right,         XKB_KEY_ANY_MOD,     "\033OC",        0,   +1},
	{ XKB_KEY_ISO_Left_Tab, MOD_SHIFT,      "\033[Z",        0,    0},
	{ XKB_KEY_Return, MOD_ALT,       "\033\r",        0,    0},
	{ XKB_KEY_Return,        XKB_KEY_ANY_MOD,     "\r",            0,    0},
	{ XKB_KEY_Insert, MOD_SHIFT,      "\033[4l",      -1,    0},
	{ XKB_KEY_Insert, MOD_SHIFT,      "\033[2;2~",    +1,    0},
	{ XKB_KEY_Insert, MOD_CTRL,    "\033[L",       -1,    0},
	{ XKB_KEY_Insert, MOD_CTRL,    "\033[2;5~",    +1,    0},
	{ XKB_KEY_Insert,        XKB_KEY_ANY_MOD,     "\033[4h",      -1,    0},
	{ XKB_KEY_Insert,        XKB_KEY_ANY_MOD,     "\033[2~",      +1,    0},
	{ XKB_KEY_Delete, MOD_CTRL,    "\033[M",       -1,    0},
	{ XKB_KEY_Delete, MOD_CTRL,    "\033[3;5~",    +1,    0},
	{ XKB_KEY_Delete, MOD_SHIFT,      "\033[2K",      -1,    0},
	{ XKB_KEY_Delete, MOD_SHIFT,      "\033[3;2~",    +1,    0},
	{ XKB_KEY_Delete,        XKB_KEY_ANY_MOD,     "\033[P",       -1,    0},
	{ XKB_KEY_Delete,        XKB_KEY_ANY_MOD,     "\033[3~",      +1,    0},
	{ XKB_KEY_BackSpace,     XKB_KEY_NO_MOD,      "\177",          0,    0},
	{ XKB_KEY_BackSpace, MOD_ALT,       "\033\177",      0,    0},
	{ XKB_KEY_Home, MOD_SHIFT,      "\033[2J",       0,   -1},
	{ XKB_KEY_Home, MOD_SHIFT,      "\033[1;2H",     0,   +1},
	{ XKB_KEY_Home,          XKB_KEY_ANY_MOD,     "\033[H",        0,   -1},
	{ XKB_KEY_Home,          XKB_KEY_ANY_MOD,     "\033[1~",       0,   +1},
	{ XKB_KEY_End, MOD_CTRL,    "\033[J",       -1,    0},
	{ XKB_KEY_End, MOD_CTRL,    "\033[1;5F",    +1,    0},
	{ XKB_KEY_End, MOD_SHIFT,      "\033[K",       -1,    0},
	{ XKB_KEY_End, MOD_SHIFT,      "\033[1;2F",    +1,    0},
	{ XKB_KEY_End,           XKB_KEY_ANY_MOD,     "\033[4~",       0,    0},
	{ XKB_KEY_Prior, MOD_CTRL,    "\033[5;5~",     0,    0},
	{ XKB_KEY_Prior, MOD_SHIFT,      "\033[5;2~",     0,    0},
	{ XKB_KEY_Prior,         XKB_KEY_ANY_MOD,     "\033[5~",       0,    0},
	{ XKB_KEY_Next, MOD_CTRL,    "\033[6;5~",     0,    0},
	{ XKB_KEY_Next, MOD_SHIFT,      "\033[6;2~",     0,    0},
	{ XKB_KEY_Next,          XKB_KEY_ANY_MOD,     "\033[6~",       0,    0},
	{ XKB_KEY_F1,            XKB_KEY_NO_MOD,      "\033OP" ,       0,    0},
	{ XKB_KEY_F1, MOD_SHIFT,      "\033[1;2P",     0,    0},
	{ XKB_KEY_F1, MOD_CTRL,    "\033[1;5P",     0,    0},
	{ XKB_KEY_F1, MOD_MOD4,       "\033[1;6P",     0,    0},
	{ XKB_KEY_F1, MOD_ALT,       "\033[1;3P",     0,    0},
	{ XKB_KEY_F1, MOD_MOD3,       "\033[1;4P",     0,    0},
	{ XKB_KEY_F2,            XKB_KEY_NO_MOD,      "\033OQ" ,       0,    0},
	{ XKB_KEY_F2, MOD_SHIFT,      "\033[1;2Q",     0,    0},
	{ XKB_KEY_F2, MOD_CTRL,    "\033[1;5Q",     0,    0},
	{ XKB_KEY_F2, MOD_MOD4,       "\033[1;6Q",     0,    0},
	{ XKB_KEY_F2, MOD_ALT,       "\033[1;3Q",     0,    0},
	{ XKB_KEY_F2, MOD_MOD3,       "\033[1;4Q",     0,    0},
	{ XKB_KEY_F3,            XKB_KEY_NO_MOD,      "\033OR" ,       0,    0},
	{ XKB_KEY_F3, MOD_SHIFT,      "\033[1;2R",     0,    0},
	{ XKB_KEY_F3, MOD_CTRL,    "\033[1;5R",     0,    0},
	{ XKB_KEY_F3, MOD_MOD4,       "\033[1;6R",     0,    0},
	{ XKB_KEY_F3, MOD_ALT,       "\033[1;3R",     0,    0},
	{ XKB_KEY_F3, MOD_MOD3,       "\033[1;4R",     0,    0},
	{ XKB_KEY_F4,            XKB_KEY_NO_MOD,      "\033OS" ,       0,    0},
	{ XKB_KEY_F4, MOD_SHIFT,      "\033[1;2S",     0,    0},
	{ XKB_KEY_F4, MOD_CTRL,    "\033[1;5S",     0,    0},
	{ XKB_KEY_F4, MOD_MOD4,       "\033[1;6S",     0,    0},
	{ XKB_KEY_F4, MOD_ALT,       "\033[1;3S",     0,    0},
	{ XKB_KEY_F5,            XKB_KEY_NO_MOD,      "\033[15~",      0,    0},
	{ XKB_KEY_F5, MOD_SHIFT,      "\033[15;2~",    0,    0},
	{ XKB_KEY_F5, MOD_CTRL,    "\033[15;5~",    0,    0},
	{ XKB_KEY_F5, MOD_MOD4,       "\033[15;6~",    0,    0},
	{ XKB_KEY_F5, MOD_ALT,       "\033[15;3~",    0,    0},
	{ XKB_KEY_F6,            XKB_KEY_NO_MOD,      "\033[17~",      0,    0},
	{ XKB_KEY_F6, MOD_SHIFT,      "\033[17;2~",    0,    0},
	{ XKB_KEY_F6, MOD_CTRL,    "\033[17;5~",    0,    0},
	{ XKB_KEY_F6, MOD_MOD4,       "\033[17;6~",    0,    0},
	{ XKB_KEY_F6, MOD_ALT,       "\033[17;3~",    0,    0},
	{ XKB_KEY_F7,            XKB_KEY_NO_MOD,      "\033[18~",      0,    0},
	{ XKB_KEY_F7, MOD_SHIFT,      "\033[18;2~",    0,    0},
	{ XKB_KEY_F7, MOD_CTRL,    "\033[18;5~",    0,    0},
	{ XKB_KEY_F7, MOD_MOD4,       "\033[18;6~",    0,    0},
	{ XKB_KEY_F7, MOD_ALT,       "\033[18;3~",    0,    0},
	{ XKB_KEY_F8,            XKB_KEY_NO_MOD,      "\033[19~",      0,    0},
	{ XKB_KEY_F8, MOD_SHIFT,      "\033[19;2~",    0,    0},
	{ XKB_KEY_F8, MOD_CTRL,    "\033[19;5~",    0,    0},
	{ XKB_KEY_F8, MOD_MOD4,       "\033[19;6~",    0,    0},
	{ XKB_KEY_F8, MOD_ALT,       "\033[19;3~",    0,    0},
	{ XKB_KEY_F9,            XKB_KEY_NO_MOD,      "\033[20~",      0,    0},
	{ XKB_KEY_F9, MOD_SHIFT,      "\033[20;2~",    0,    0},
	{ XKB_KEY_F9, MOD_CTRL,    "\033[20;5~",    0,    0},
	{ XKB_KEY_F9, MOD_MOD4,       "\033[20;6~",    0,    0},
	{ XKB_KEY_F9, MOD_ALT,       "\033[20;3~",    0,    0},
	{ XKB_KEY_F10,           XKB_KEY_NO_MOD,      "\033[21~",      0,    0},
	{ XKB_KEY_F10, MOD_SHIFT,      "\033[21;2~",    0,    0},
	{ XKB_KEY_F10, MOD_CTRL,    "\033[21;5~",    0,    0},
	{ XKB_KEY_F10, MOD_MOD4,       "\033[21;6~",    0,    0},
	{ XKB_KEY_F10, MOD_ALT,       "\033[21;3~",    0,    0},
	{ XKB_KEY_F11,           XKB_KEY_NO_MOD,      "\033[23~",      0,    0},
	{ XKB_KEY_F11, MOD_SHIFT,      "\033[23;2~",    0,    0},
	{ XKB_KEY_F11, MOD_CTRL,    "\033[23;5~",    0,    0},
	{ XKB_KEY_F11, MOD_MOD4,       "\033[23;6~",    0,    0},
	{ XKB_KEY_F11, MOD_ALT,       "\033[23;3~",    0,    0},
	{ XKB_KEY_F12,           XKB_KEY_NO_MOD,      "\033[24~",      0,    0},
	{ XKB_KEY_F12, MOD_SHIFT,      "\033[24;2~",    0,    0},
	{ XKB_KEY_F12, MOD_CTRL,    "\033[24;5~",    0,    0},
	{ XKB_KEY_F12, MOD_MOD4,       "\033[24;6~",    0,    0},
	{ XKB_KEY_F12, MOD_ALT,       "\033[24;3~",    0,    0},
	{ XKB_KEY_F13,           XKB_KEY_NO_MOD,      "\033[1;2P",     0,    0},
	{ XKB_KEY_F14,           XKB_KEY_NO_MOD,      "\033[1;2Q",     0,    0},
	{ XKB_KEY_F15,           XKB_KEY_NO_MOD,      "\033[1;2R",     0,    0},
	{ XKB_KEY_F16,           XKB_KEY_NO_MOD,      "\033[1;2S",     0,    0},
	{ XKB_KEY_F17,           XKB_KEY_NO_MOD,      "\033[15;2~",    0,    0},
	{ XKB_KEY_F18,           XKB_KEY_NO_MOD,      "\033[17;2~",    0,    0},
	{ XKB_KEY_F19,           XKB_KEY_NO_MOD,      "\033[18;2~",    0,    0},
	{ XKB_KEY_F20,           XKB_KEY_NO_MOD,      "\033[19;2~",    0,    0},
	{ XKB_KEY_F21,           XKB_KEY_NO_MOD,      "\033[20;2~",    0,    0},
	{ XKB_KEY_F22,           XKB_KEY_NO_MOD,      "\033[21;2~",    0,    0},
	{ XKB_KEY_F23,           XKB_KEY_NO_MOD,      "\033[23;2~",    0,    0},
	{ XKB_KEY_F24,           XKB_KEY_NO_MOD,      "\033[24;2~",    0,    0},
	{ XKB_KEY_F25,           XKB_KEY_NO_MOD,      "\033[1;5P",     0,    0},
	{ XKB_KEY_F26,           XKB_KEY_NO_MOD,      "\033[1;5Q",     0,    0},
	{ XKB_KEY_F27,           XKB_KEY_NO_MOD,      "\033[1;5R",     0,    0},
	{ XKB_KEY_F28,           XKB_KEY_NO_MOD,      "\033[1;5S",     0,    0},
	{ XKB_KEY_F29,           XKB_KEY_NO_MOD,      "\033[15;5~",    0,    0},
	{ XKB_KEY_F30,           XKB_KEY_NO_MOD,      "\033[17;5~",    0,    0},
	{ XKB_KEY_F31,           XKB_KEY_NO_MOD,      "\033[18;5~",    0,    0},
	{ XKB_KEY_F32,           XKB_KEY_NO_MOD,      "\033[19;5~",    0,    0},
	{ XKB_KEY_F33,           XKB_KEY_NO_MOD,      "\033[20;5~",    0,    0},
	{ XKB_KEY_F34,           XKB_KEY_NO_MOD,      "\033[21;5~",    0,    0},
	{ XKB_KEY_F35,           XKB_KEY_NO_MOD,      "\033[23;5~",    0,    0},
};

/*
 * Selection types' masks.
 * Use the same masks as usual.
 * Button1Mask is always unset, to make masks match between ButtonPress.
 * ButtonRelease and MotionNotify.
 * If no match is found, regular selection is used.
 */
static uint selmasks[] = {
	[SEL_RECTANGULAR] = MOD_ALT, /* 8 */
};

/*
 * Printable characters in ASCII, used to estimate the advance width
 * of single wide characters.
 */
static char ascii_printable[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";
