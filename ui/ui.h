/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - ECal's GEM interface: one window with a toolbar, the
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
#define TX_LIGHT    8	/* drawn as normal text now: grey was hard to read */
#define TX_LTR     16	/* left-to-right even when it starts in Hebrew */
#define TX_CENTER  32
#define TX_UNDER   64
#define TX_DIM    128	/* what can't be used (disabled controls): light grey, else dotted */
#define TX_GREY   256	/* secondary text: labels, hints, Hebrew dates (dark grey) */
#define TX_PALE   512	/* what recedes: other months' days, hidden calendars (light grey) */
void fill(GRECT *r, short color);
void set_wrmode(short mode);	/* VDI writing mode, through draw.c's cache */
void fill_pat(GRECT *r, short color, short interior, short style);
void frame(GRECT *r, short color);
void xor_frame(GRECT *r);	/* an outline in XOR (twice: gone) */
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
void st_ram_init(void);		/* at start-up */
#define MARK_LOOP 0
#define MARK_BELL 1
short mark_w(void);		/* a mark's size for the font: 16 or 8 pixels */
void draw_mark(short x, short y, short size, int which, short color, const GRECT *clip);	/* top left */
void *st_ram(long n);		/* 1 KB of ST RAM for a bitmap to blit (one at a time) */

/* colours of a calendar on this screen: a pen and a fill pattern */
typedef struct {
	short color;		/* VDI colour index for bars and dots */
	short interior, style;	/* fill for event boxes (a light tint) */
} PEN;
void pen_of(int cal, PEN *p);
void pen_color(short k, PEN *p);	/* colour k of the eight */
#include "popup.h"		/* popup(), popup_area: common/ui */
/* a popup over the window: what it covered is repainted at once, before
   the choice opens a dialog (GEM's redraw message would wait for it) */
short win_popup(short x, short y, const char *const *labels, short n);
short red_color(void);		/* "today" */
short grey_color(void);		/* lines (the light grey) */
short text_grey(int pale);	/* the dark (0) or light (1) grey for text; 1 (black) without */

/* ---- the window (win.c) ---- */
extern short win_h;		/* AES handle, -1 when closed */
extern short win_iconified;	/* shown as the icon */
extern GRECT work;		/* its work area */
void win_open(void);
void win_close(void);
void win_redraw(GRECT *area);	/* NULL: everything */
void win_buffer_init(void);
const char *win_drawing(void);	/* how the window is drawn, for the log */
void draw_forget(void);		/* the VDI attributes set again at the next use */
void win_scroll(const GRECT *a, short dy);	/* a's content up by dy pixels */
void win_message(short *msg);
void win_status(const char *s);	/* the info line */
void win_frame_refresh(void);
int  win_instant(short mx, short my);	/* a toolbar button: no double click to wait for */
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
void views_scroll_to(short hour);	/* scroll there: the events move too */
int  views_tip(short mx, short my, char *title, int tn, char *when, int wn);	/* the event under the mouse */
void views_copy(void);				/* ^C: the event chosen, to the clipboard */
void views_invalidate(void);	/* events changed: collect again */
void views_draw(GRECT *clip);
int  views_click(short mx, short my, short clicks, short kstate, short button);
int  views_key(short kstate, short key);
void views_title(char *out, int size);	/* "October 2026" */
void views_step(int dir);	/* previous / next period */
void views_today(void);
void views_goto(long day, short view);
void views_select_none(void);
void views_arrow(short what);	/* WM_ARROWED: 0 page up .. 3 line down */
void views_slide(short pos);	/* WM_VSLID, 0..1000 */
void views_slider(long *size, long *pos);	/* the slider's, 0..1000 */
int  occ_find_again(void);	/* after a reload, find the selected one again */

/* ---- the sidebar (sidebar.c) ---- */
void side_draw(GRECT *clip);
int  side_click(short mx, short my, short clicks, short button);
int  side_instant(short mx, short my);	/* over a tick box: no double click to wait for */

/* ---- dialogs drawn by us (field.c) ---- */
enum { FT_LABEL, FT_TEXT, FT_PASS, FT_BUTTON, FT_CHECK, FT_POPUP, FT_MULTI, FT_HEAD, FT_COLOR, FT_PEOPLE };
#define FF_DEFAULT  1	/* Return presses it */
#define FF_CANCEL   2	/* Esc presses it */
#define FF_DISABLED 4
#define FF_LTR      8	/* text field for addresses, numbers: never right to left */
#define FF_DANGER  16	/* the Delete button */
#define FF_SHOWN   32	/* a password field shown in clear (its eye clicked) */
#define FF_DIGITS  64	/* a number field: only digits can be typed */
#define FF_ACTION 128	/* a button that acts in the dialog (changed()), leaving it open */
typedef struct {
	short type, flags;
	short x, y, w, h;	/* in characters */
	const char *label;
	char *buf;		/* text fields */
	short max;		/* its size */
	short val;		/* check: 0/1; popup: chosen item; color: index */
	const char *const *items;	/* popup labels; people: their names */
	short nitems;
	const char *const *tips;	/* people: what pointing at each shows (the address) */
	short cur, first;	/* cursor and scroll (text fields) */
	short page;		/* in a dialog with sections: its section, 1...; 0 on all */
} FIELD;
typedef struct DLG DLG;
struct DLG {
	const char *title;
	short w, h;		/* in characters */
	FIELD *f;
	short n;
	short focus;
	/* called when a popup, check, color or people field changes, or an
	   FF_ACTION button is pressed; may redraw */
	void (*changed)(DLG *d, short i);
	/* Delete pressed in people field i: take its chosen one (val) away */
	void (*removed)(DLG *d, short i);
	void *ud;
	/* sections, as in EMail's Settings: their names in a list on the
	   left (side characters wide), the chosen one's fields on the right
	   under its name */
	const char *const *pages;
	short npages, page, side;
	/* names came from EContact (people_ask.c): show them; may redraw */
	void (*names)(DLG *d);
};
short dlg_run(DLG *d);		/* the button pressed (field index), -1 Esc */
void  dlg_redraw_field(DLG *d, short i);
void  dlg_redraw(DLG *d);		/* all of it (after a dialog opened from it) */
extern void (*dlg_after)(void);	/* when the last dialog closes */
void  dlg_names_came(void);
/* people_init's picked: the people chosen in EContact's window (editor.c) */
#include "people_ask.h"
void  editor_people_picked(const PERSON *list, int n);	/* people_init's names_came: the open dialog's names() */
#include "alert.h"		/* alert(): common/ui */
/* a list to pick from (a calendar, a zone): index or -1 */
short dlg_pick(const char *title, const char *const *items, short n, short sel);
int   dlg_ask(const char *title, const char *label, char *buf, short len);

/* ---- the dialogs (dialogs.c, editor.c) ---- */
int  editor_open(EVENT *e, CTIME occ_start, CTIME orig);	/* 1 if changed; orig: OCC.orig */
void editor_new(long day, short hour_min, short allday);
void editor_new_titled(long day, const char *text);	/* text dropped: a new event */
void editor_new_from(EVENT *e);	/* an .ics file's event (an invitation), in the dialog; e is freed */
/* an occurrence dragged to new_start (the view's wall time): 1 if saved */
int  editor_move(EVENT *e, CTIME occ_start, CTIME orig, CTIME new_start);
void dlg_subscribe(const char *url);
void dlg_add_account(void);
int  dlg_account(int a);	/* 1 changed, -1 deleted */
int  dlg_calendar(int c);	/* name, colour, alarms of one calendar; 1 if changed */
void dlg_settings(void);
void dlg_connection(void);
void dlg_goto(void);
void dlg_find(void);
void no_memory(void);		/* the alert when a dialog finds no memory for itself */
void dlg_about(void);
void icon_today(char *s, int size);	/* "Oct 14" */
void icon_window(GRECT *area);		/* the iconified window: icon and today */
void dlg_keys(void);
#include "help.h"		/* help_init (at start-up), help_show: common/ui */
void help_open(void);		/* ECAL.HYP in EHypView or ST-Guide, or the keys */
void font_apply(void);		/* opt.font_id/pt -> the VDI font, cw, ch */
int  font_boot(int want);	/* the accessory, before its first event call */
int  font_gdos_loaded(void);
void font_menu(void);		/* the font selector (font.c) */
void font_reselect(void);	/* the text font again, after drawing in another */
/* how the text font gets its Hebrew letters */
#define HM_MAP     0		/* where opt.hebfont says (system and bitmap fonts) */
#define HM_UNICODE 1		/* a vector font's own, found by Unicode (NVDI) */
#define HM_SYSTEM  2		/* a vector font without them: the system font's */
extern short heb_mode;
extern unsigned short heb_idx[27];
void font_sys_push(void);	/* the system font, for a dialog or popup */
void font_sys_pop(void);	/* back to the window's font */
const char *zone_pick(const char *current);	/* a zone name, or NULL */

/* ---- main.c ---- */
void status(const char *msg);
void status_updated(void);	/* back to "Updated 10:42" */
void busy(int on);
void cmd_refresh(int force);
void cmd_menu(short x, short y);	/* the toolbar's menu */
void cmd_delete_selected(void);
void cmd_new_event(void);
void cmd_import(int cal);
void after_change(void);		/* events changed: redraw all */
extern short hebrew_kbd;
void set_hebrew_kbd(short on);
void settings_later(void);	/* save the settings in a few seconds (main.c) */
void mem_ran_out(void);		/* MEMKB= doubled for the next start (main.c) */
const char *mem_full_advice(void);	/* a "memory is full" alert's last lines */
void settings_flush(void);	/* now, if anything is waiting */
void closing_down(int quitting);	/* the last steps, on the info line */
unsigned char key_char(short kstate, short key);	/* honours the Hebrew layout */
void falcon_dsp_init(void);		/* dsp.c */
const char *falcon_dsp_state(void);
void falcon_dsp_ec_init(void);		/* dsp_ec.c (dsp_ec_none.c in the ST edition) */
const char *falcon_dsp_ec_state(void);
int  falcon_mode_set(int on);
extern short quit_now;
void set_logging(void);

#define K_ALT 0x08
#define KEY_SCAN(k)  (((unsigned short)(k)) >> 8)
#define KEY_ASCII(k) ((unsigned char)((k) & 0xff))

#endif
