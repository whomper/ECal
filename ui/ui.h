/*
 * ui.h - Calendar's GEM interface: one window with a toolbar, the
 * calendar list on the left and the day, week, month or year on the
 * right, much like the Mac's Calendar; dialogs drawn by field.c so that
 * every text field takes Hebrew.
 */
#ifndef UI_H
#define UI_H

#include "tos.h"
#include "gem.h"
#include "../src/calstore.h"

typedef struct { short x, y, w, h; } GRECT;

/* screen and font */
extern short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
extern short gl_wchar, gl_hchar;
extern short ap_id;
extern short is_acc;		/* started as a desk accessory */
extern short ncolors;		/* 2, 4, 16, 256 */

/* ---- drawing (draw.c) ---- */
#define TX_BOLD     1
#define TX_INVERSE  2	/* white text */
#define TX_RIGHT    4	/* right-to-left text against the right edge */
#define TX_LIGHT    8
#define TX_LTR     16	/* left-to-right even when it starts in Hebrew */
#define TX_CENTER  32
#define TX_UNDER   64
void fill(GRECT *r, short color);
void set_wrmode(short mode);	/* VDI writing mode, through draw.c's cache */
void fill_pat(GRECT *r, short color, short interior, short style);
void frame(GRECT *r, short color);
void clip_on(GRECT *r);
void clip_off(void);
void text_at(short x, short y, const char *s, long n, short cols, short flags);
void text_color(short color);	/* for the next text_at; 1 = black, reset after */
void hline(short x1, short x2, short y);
void vline(short x, short y1, short y2);
void line_col(short x1, short y1, short x2, short y2, short color);
long wrap_text(const char *s, long n, short width, long *breaks, long max);
short line_rtl(const char *s, long n);
void heb_font_map(char *s, long n, short where);
int  rect_hit(const GRECT *r, short x, short y);
int  rect_and(GRECT *a, const GRECT *b);	/* a = a & b; 0 if empty */

/* colours of a calendar on this screen: a pen and a fill pattern */
typedef struct {
	short color;		/* VDI colour index for bars and dots */
	short interior, style;	/* fill for event boxes (a light tint) */
} PEN;
void pen_of(int cal, PEN *p);
void pen_color(short k, PEN *p);	/* colour k of the eight */
short popup(short x, short y, const char *const *labels, short n);	/* popup.c */
short red_color(void);		/* "today" */
short grey_color(void);		/* lines, other months' days */

/* ---- the window (win.c) ---- */
extern short win_h;		/* AES handle, -1 when closed */
extern GRECT work;		/* its work area */
void win_open(void);
void win_close(void);
void win_redraw(GRECT *area);	/* NULL: everything */
void win_message(short *msg);
void win_status(const char *s);	/* the info line */
void win_frame_refresh(void);	/* title and info line again */
int  win_mouse(short mx, short my, short button, short clicks, short kstate);
int  win_key(short kstate, short key);
void win_layout(void);		/* after a size or font change */
void win_title(void);
void win_sliders(void);

/* layout: filled in by win_layout */
extern GRECT r_tool, r_side, r_view;

/* ---- the views (views.c) ---- */
extern long cur_day;		/* the selected day (days since 1970, view zone) */
extern short cur_view;
extern OCC sel_occ;		/* the selected occurrence (ev NULL: none) */
extern short day_scroll;	/* first hour shown in day and week views */
void views_invalidate(void);	/* events changed: collect again */
void views_draw(GRECT *clip);
int  views_click(short mx, short my, short clicks, short kstate, short button);
int  views_key(short kstate, short key);
void views_title(char *out, int size);	/* "October 2026" */
void views_step(int dir);	/* previous / next period */
void views_today(void);
void views_goto(long day, short view);
void views_select_none(void);
long views_hours_visible(void);
int  occ_find_again(void);	/* after a reload, find the selected one again */

/* ---- the sidebar (sidebar.c) ---- */
void side_draw(GRECT *clip);
int  side_click(short mx, short my, short clicks, short button);

/* ---- dialogs drawn by us (field.c) ---- */
enum { FT_LABEL, FT_TEXT, FT_PASS, FT_BUTTON, FT_CHECK, FT_POPUP, FT_MULTI, FT_HEAD, FT_COLOR };
#define FF_DEFAULT  1	/* Return presses it */
#define FF_CANCEL   2	/* Esc presses it */
#define FF_DISABLED 4
#define FF_LTR      8	/* text field for addresses, numbers: never right to left */
#define FF_DANGER  16	/* the Delete button */
typedef struct {
	short type, flags;
	short x, y, w, h;	/* in characters */
	const char *label;
	char *buf;		/* text fields */
	short max;		/* its size */
	short val;		/* check: 0/1; popup: chosen item; color: index */
	const char *const *items;	/* popup labels */
	short nitems;
	short cur, first;	/* cursor and scroll (text fields) */
} FIELD;
typedef struct DLG DLG;
struct DLG {
	const char *title;
	short w, h;		/* in characters */
	FIELD *f;
	short n;
	short focus;
	/* called when a popup, check or color changes; may redraw */
	void (*changed)(DLG *d, short i);
	void *ud;
};
short dlg_run(DLG *d);		/* the button pressed (field index), -1 Esc */
void  dlg_redraw_field(DLG *d, short i);
void  dlg_redraw(DLG *d);		/* all of it (after a dialog opened from it) */
extern void (*dlg_after)(void);	/* when the last dialog closes */
short alert(short def, const char *fmt, ...);
/* a list to pick from (a calendar, a zone): index or -1 */
short dlg_pick(const char *title, const char *const *items, short n, short sel);
int   dlg_ask(const char *title, const char *label, char *buf, short len);

/* ---- the dialogs (dialogs.c, editor.c) ---- */
int  editor_open(EVENT *e, CTIME occ_start, CTIME orig);	/* 1 if changed; orig: OCC.orig */
void editor_new(long day, short hour_min, short allday);
void dlg_add_account(void);
int  dlg_account(int a);	/* 1 changed, -1 deleted */
void dlg_calendar(int c);	/* name, colour, alarms of one calendar */
void dlg_settings(void);
void dlg_connection(void);
void dlg_goto(void);
void dlg_find(void);
void dlg_about(void);
void dlg_keys(void);
const char *zone_pick(const char *current);	/* a zone name, or NULL */

/* ---- main.c ---- */
void status(const char *msg);
void busy(int on);
void cmd_refresh(int force);
void cmd_menu(short x, short y);	/* the toolbar's menu */
void cmd_delete_selected(void);
void cmd_new_event(void);
void cmd_import(int cal);
void after_change(void);		/* events changed: redraw all */
extern short hebrew_kbd;
void set_hebrew_kbd(short on);
unsigned char key_char(short kstate, short key);	/* honours the Hebrew layout */
void falcon_dsp_init(void);		/* dsp.c */
const char *falcon_dsp_state(void);
int  falcon_mode_set(int on);
extern short quit_now;
void set_logging(void);

#define K_ALT 0x08
#define KEY_SCAN(k)  (((unsigned short)(k)) >> 8)
#define KEY_ASCII(k) ((unsigned char)((k) & 0xff))

#endif
