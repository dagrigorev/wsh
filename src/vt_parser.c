#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vt_parser.h"
#include "screen.h"
#include "util.h"

/* ─── Init ───────────────────────────────────────────────────────────────── */

void vt_parser_init(VtParser *vt, ScreenBuffer *screen) {
    memset(vt, 0, sizeof(*vt));
    vt->state  = VT_GROUND;
    vt->screen = screen;
    vt->charset_g0 = 0;
    vt->charset_g1 = 0;
}

void vt_parser_reset(VtParser *vt) {
    ScreenBuffer *s = vt->screen;
    void (*cb)(const char*, void*) = vt->on_title;
    void *ud = vt->userdata;
    vt_parser_init(vt, s);
    vt->on_title  = cb;
    vt->userdata  = ud;
}

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static int param(VtParser *vt, int idx, int def) {
    if (idx < 0 || idx >= vt->param_count) return def;
    return vt->params[idx] == 0 ? def : vt->params[idx];
}

static int param_raw(VtParser *vt, int idx, int def) {
    if (idx < 0 || idx >= vt->param_count) return def;
    return vt->params[idx];
}

static void clear_params(VtParser *vt) {
    memset(vt->params, 0, sizeof(vt->params));
    vt->param_count   = 0;
    vt->question_mark = false;
    vt->bang          = false;
    vt->intermediate  = 0;
}

/* ─── SGR (Select Graphic Rendition) ────────────────────────────────────── */

static void handle_sgr(VtParser *vt) {
    CellAttr *a = &vt->screen->current_attr;
    int i = 0;
    if (vt->param_count == 0) {
        /* Reset all */
        memset(a, 0, sizeof(*a));
        a->fg_idx = 7;
        a->bg_idx = 0;
        return;
    }
    while (i < vt->param_count) {
        int p = vt->params[i];
        switch (p) {
            case 0:  memset(a, 0, sizeof(*a)); a->fg_idx = 7; a->bg_idx = 0; break;
            case 1:  a->bold      = 1; break;
            case 2:  a->dim       = 1; break;
            case 3:  a->italic    = 1; break;
            case 4:  a->underline = 1; break;
            case 5:  a->blink     = 1; break;
            case 7:  a->reverse   = 1; break;
            case 9:  a->strikethrough = 1; break;
            case 22: a->bold = 0; a->dim = 0; break;
            case 23: a->italic    = 0; break;
            case 24: a->underline = 0; break;
            case 25: a->blink     = 0; break;
            case 27: a->reverse   = 0; break;
            case 29: a->strikethrough = 0; break;
            /* Foreground 30-37 */
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                a->fg_idx = (uint8_t)(p - 30); break;
            case 38: /* Extended fg */
                if (i + 2 < vt->param_count && vt->params[i+1] == 5) {
                    a->fg_idx = (uint8_t)vt->params[i+2]; i += 2;
                } else if (i + 4 < vt->param_count && vt->params[i+1] == 2) {
                    a->fg_idx = 0xFF;
                    a->fg_rgb = ((uint32_t)vt->params[i+2] << 16) |
                                ((uint32_t)vt->params[i+3] << 8)  |
                                 (uint32_t)vt->params[i+4];
                    i += 4;
                }
                break;
            case 39: a->fg_idx = 7; break;
            /* Background 40-47 */
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                a->bg_idx = (uint8_t)(p - 40); break;
            case 48: /* Extended bg */
                if (i + 2 < vt->param_count && vt->params[i+1] == 5) {
                    a->bg_idx = (uint8_t)vt->params[i+2]; i += 2;
                } else if (i + 4 < vt->param_count && vt->params[i+1] == 2) {
                    a->bg_idx = 0xFF;
                    a->bg_rgb = ((uint32_t)vt->params[i+2] << 16) |
                                ((uint32_t)vt->params[i+3] << 8)  |
                                 (uint32_t)vt->params[i+4];
                    i += 4;
                }
                break;
            case 49: a->bg_idx = 0; break;
            /* Bright fg 90-97, bright bg 100-107 */
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                a->fg_idx = (uint8_t)(p - 90 + 8); break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                a->bg_idx = (uint8_t)(p - 100 + 8); break;
            default: break;
        }
        i++;
    }
}

/* ─── CSI Dispatch ───────────────────────────────────────────────────────── */

static void dispatch_csi(VtParser *vt, char final) {
    ScreenBuffer *sb = vt->screen;
    int n, m;

    if (vt->question_mark) {
        /* Private DEC modes */
        switch (final) {
            case 'h': /* Set mode */
                for (int i = 0; i < vt->param_count; i++) {
                    switch (vt->params[i]) {
                        case 1:    sb->app_cursor_keys = true;  break;
                        case 25:   sb->cursor_visible  = true;  break;
                        case 47:   screen_enter_alt(sb);        break;
                        case 1049: screen_enter_alt(sb); screen_save_cursor(sb); break;
                        case 2004: sb->bracketed_paste = true;  break;
                        case 7:    sb->auto_wrap = true;        break;
                        default: break;
                    }
                }
                break;
            case 'l': /* Reset mode */
                for (int i = 0; i < vt->param_count; i++) {
                    switch (vt->params[i]) {
                        case 1:    sb->app_cursor_keys = false; break;
                        case 25:   sb->cursor_visible  = false; break;
                        case 47:   screen_leave_alt(sb);        break;
                        case 1049: screen_restore_cursor(sb); screen_leave_alt(sb); break;
                        case 2004: sb->bracketed_paste = false; break;
                        case 7:    sb->auto_wrap = false;       break;
                        default: break;
                    }
                }
                break;
            default: break;
        }
        return;
    }

    switch (final) {
        /* Cursor movement */
        case 'A': n = param(vt, 0, 1); screen_move_cursor(sb, 0, -n); break;
        case 'B': n = param(vt, 0, 1); screen_move_cursor(sb, 0,  n); break;
        case 'C': n = param(vt, 0, 1); screen_move_cursor(sb,  n, 0); break;
        case 'D': n = param(vt, 0, 1); screen_move_cursor(sb, -n, 0); break;
        case 'E': n = param(vt, 0, 1); sb->cursor_x = 0; screen_move_cursor(sb, 0, n);  break;
        case 'F': n = param(vt, 0, 1); sb->cursor_x = 0; screen_move_cursor(sb, 0, -n); break;
        case 'G': n = param(vt, 0, 1); screen_set_cursor(sb, n - 1, sb->cursor_y); break;
        case 'H': case 'f':
            n = param(vt, 0, 1); m = param(vt, 1, 1);
            screen_set_cursor(sb, m - 1, n - 1);
            break;
        /* Erase */
        case 'J': screen_erase_display(sb, param_raw(vt, 0, 0)); break;
        case 'K': screen_erase_line(sb,    param_raw(vt, 0, 0)); break;
        /* SGR */
        case 'm': handle_sgr(vt); break;
        /* Scroll region */
        case 'r':
            n = param(vt, 0, 1); m = param(vt, 1, sb->rows);
            sb->scroll_top = n - 1;
            sb->scroll_bot = m - 1;
            if (sb->scroll_top < 0) sb->scroll_top = 0;
            if (sb->scroll_bot >= sb->rows) sb->scroll_bot = sb->rows - 1;
            screen_set_cursor(sb, 0, 0);
            break;
        /* Save/restore cursor */
        case 's': screen_save_cursor(sb);    break;
        case 'u': screen_restore_cursor(sb); break;
        /* Line ops */
        case 'L': screen_insert_lines(sb, param(vt, 0, 1)); break;
        case 'M': screen_delete_lines(sb, param(vt, 0, 1)); break;
        /* Char ops */
        case 'P': screen_delete_chars(sb, param(vt, 0, 1)); break;
        case '@': screen_insert_chars(sb, param(vt, 0, 1)); break;
        case 'X': screen_erase_chars(sb,   param(vt, 0, 1)); break;
        /* Scroll */
        case 'S': screen_scroll_up(sb,   sb->scroll_top, sb->scroll_bot, param(vt, 0, 1)); break;
        case 'T': screen_scroll_down(sb, sb->scroll_top, sb->scroll_bot, param(vt, 0, 1)); break;
        /* Cursor column absolute */
        case '`': screen_set_cursor(sb, param(vt, 0, 1) - 1, sb->cursor_y); break;
        /* Cursor row absolute */
        case 'd': screen_set_cursor(sb, sb->cursor_x, param(vt, 0, 1) - 1); break;
        /* DA */
        case 'c': /* ignore device attributes */ break;
        /* DSR */
        case 'n': /* ignore status report */ break;
        /* Set mode / reset mode */
        case 'h':
            for (int i = 0; i < vt->param_count; i++) {
                if (vt->params[i] == 4) sb->insert_mode = true;
            }
            break;
        case 'l':
            for (int i = 0; i < vt->param_count; i++) {
                if (vt->params[i] == 4) sb->insert_mode = false;
            }
            break;
        default: break;
    }
}

/* ─── OSC Dispatch ───────────────────────────────────────────────────────── */

static void dispatch_osc(VtParser *vt) {
    char *buf = vt->osc_buf;
    int   len = vt->osc_len;
    if (len <= 0) return;
    buf[len] = '\0';

    /* Find command number */
    int cmd = 0;
    int i   = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        cmd = cmd * 10 + (buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == ';') i++;

    switch (cmd) {
        case 0: /* Set icon name and window title */
        case 2: /* Set window title */
            if (vt->screen && len - i < (int)sizeof(vt->screen->title))
                memcpy(vt->screen->title, buf + i, (size_t)(len - i + 1));
            if (vt->on_title) vt->on_title(buf + i, vt->userdata);
            break;
        case 8: /* Hyperlinks — ignore for now */ break;
        default: break;
    }
}

/* ─── ESC dispatch ───────────────────────────────────────────────────────── */

static void dispatch_esc(VtParser *vt, char ch) {
    ScreenBuffer *sb = vt->screen;
    switch (ch) {
        case 'D': /* IND — index (scroll up 1) */
            screen_newline(sb);
            break;
        case 'E': /* NEL — next line */
            sb->cursor_x = 0;
            screen_newline(sb);
            break;
        case 'M': /* RI — reverse index */
            if (sb->cursor_y > sb->scroll_top) {
                sb->cursor_y--;
            } else {
                screen_scroll_down(sb, sb->scroll_top, sb->scroll_bot, 1);
            }
            break;
        case '7': screen_save_cursor(sb);    break;
        case '8': screen_restore_cursor(sb); break;
        case 'c': /* RIS — full reset */
            screen_erase_display(sb, 2);
            screen_set_cursor(sb, 0, 0);
            memset(&sb->current_attr, 0, sizeof(sb->current_attr));
            sb->current_attr.fg_idx = 7;
            sb->scroll_top = 0;
            sb->scroll_bot = sb->rows - 1;
            break;
        case '=': /* DECKPAM — app keypad mode */ break;
        case '>': /* DECKPNM — normal keypad mode */ break;
        default: break;
    }
}

/* ─── Feed bytes ─────────────────────────────────────────────────────────── */

static void process_char(VtParser *vt, uint32_t cp);

static void feed_utf8_byte(VtParser *vt, uint8_t b) {
    /* Collect UTF-8 sequence */
    if (vt->utf8_rem > 0) {
        if ((b & 0xC0) == 0x80) {
            vt->utf8_cp = (vt->utf8_cp << 6) | (b & 0x3F);
            vt->utf8_rem--;
            if (vt->utf8_rem == 0) process_char(vt, vt->utf8_cp);
            return;
        }
        /* Invalid continuation — fall through as new start */
        vt->utf8_rem = 0;
    }

    if (b < 0x80) {
        process_char(vt, (uint32_t)b);
    } else if (b < 0xC0) {
        /* Spurious continuation — ignore */
    } else if (b < 0xE0) {
        vt->utf8_cp  = b & 0x1F;
        vt->utf8_rem = 1;
    } else if (b < 0xF0) {
        vt->utf8_cp  = b & 0x0F;
        vt->utf8_rem = 2;
    } else {
        vt->utf8_cp  = b & 0x07;
        vt->utf8_rem = 3;
    }
}

static void process_char(VtParser *vt, uint32_t cp) {
    ScreenBuffer *sb = vt->screen;

    /* C0 controls handled in most states */
    if (cp < 0x20 || cp == 0x7F) {
        switch (cp) {
            case '\r': screen_carriage_return(sb); return;
            case '\n': case '\v': case '\f': screen_newline(sb); return;
            case '\b': if (sb->cursor_x > 0) sb->cursor_x--; return;
            case '\t': {
                /* Tab stop every 8 columns */
                int next = ((sb->cursor_x / 8) + 1) * 8;
                if (next >= sb->cols) next = sb->cols - 1;
                sb->cursor_x = next;
                return;
            }
            case '\a': /* BEL — visual bell handled by renderer */ return;
            case '\x1B': vt->state = VT_ESCAPE; clear_params(vt); return;
            case '\x9B': vt->state = VT_CSI_ENTRY; clear_params(vt); return;
            case '\x0E': vt->use_g1 = true;  return; /* SO */
            case '\x0F': vt->use_g1 = false; return; /* SI */
            default: return;
        }
    }

    switch (vt->state) {
        case VT_GROUND:
            screen_put_char(sb, cp, &sb->current_attr);
            break;

        case VT_ESCAPE:
            if (cp >= 0x20 && cp <= 0x2F) {
                vt->intermediate = (char)cp;
                vt->state = VT_ESCAPE_INTERMEDIATE;
            } else if (cp == '[') {
                vt->state = VT_CSI_ENTRY;
                clear_params(vt);
            } else if (cp == ']') {
                vt->state = VT_OSC_STRING;
                vt->osc_len = 0;
            } else if (cp == 'P') {
                vt->state = VT_DCS_STRING;
                vt->osc_len = 0;
            } else if (cp == 'N') {
                vt->state = VT_SS2;
            } else if (cp == 'O') {
                vt->state = VT_SS3;
            } else if (cp >= 0x40 && cp <= 0x5F) {
                dispatch_esc(vt, (char)cp);
                vt->state = VT_GROUND;
            } else if (cp >= 0x60 && cp <= 0x7E) {
                dispatch_esc(vt, (char)cp);
                vt->state = VT_GROUND;
            } else {
                vt->state = VT_GROUND;
            }
            break;

        case VT_ESCAPE_INTERMEDIATE:
            if (cp >= 0x30 && cp <= 0x7E) {
                /* Select character set */
                if (vt->intermediate == '(') vt->charset_g0 = (cp == '0') ? 1 : 0;
                if (vt->intermediate == ')') vt->charset_g1 = (cp == '0') ? 1 : 0;
                vt->state = VT_GROUND;
            }
            break;

        case VT_CSI_ENTRY:
            if (cp == '?') { vt->question_mark = true; }
            else if (cp == '!') { vt->bang = true; }
            else if (cp >= '0' && cp <= '9') {
                vt->params[vt->param_count] = (int)(cp - '0');
                vt->state = VT_CSI_PARAM;
                break;
            } else if (cp == ';') {
                vt->param_count++;
                if (vt->param_count < VT_MAX_PARAMS) vt->params[vt->param_count] = 0;
                break;
            } else if (cp >= 0x40 && cp <= 0x7E) {
                vt->param_count++;
                dispatch_csi(vt, (char)cp);
                vt->state = VT_GROUND;
                break;
            }
            vt->state = VT_CSI_PARAM;
            break;

        case VT_CSI_PARAM:
            if (cp >= '0' && cp <= '9') {
                if (vt->param_count < VT_MAX_PARAMS)
                    vt->params[vt->param_count] = vt->params[vt->param_count] * 10 + (int)(cp - '0');
            } else if (cp == ';') {
                vt->param_count++;
                if (vt->param_count < VT_MAX_PARAMS) vt->params[vt->param_count] = 0;
            } else if (cp >= 0x20 && cp <= 0x2F) {
                vt->intermediate = (char)cp;
                vt->state = VT_CSI_INTERMEDIATE;
            } else if (cp >= 0x40 && cp <= 0x7E) {
                vt->param_count++;
                dispatch_csi(vt, (char)cp);
                vt->state = VT_GROUND;
            } else {
                vt->state = VT_CSI_IGNORE;
            }
            break;

        case VT_CSI_INTERMEDIATE:
            if (cp >= 0x40 && cp <= 0x7E) {
                dispatch_csi(vt, (char)cp);
                vt->state = VT_GROUND;
            } else if (cp < 0x20) {
                vt->state = VT_CSI_IGNORE;
            }
            break;

        case VT_CSI_IGNORE:
            if (cp >= 0x40 && cp <= 0x7E) vt->state = VT_GROUND;
            break;

        case VT_OSC_STRING:
            if (cp == '\a' || cp == '\x9C') {
                dispatch_osc(vt);
                vt->state = VT_GROUND;
            } else if (cp == '\x1B') {
                /* ESC might be start of ST (ESC \) */
                /* For simplicity, treat as end */
                dispatch_osc(vt);
                vt->state = VT_GROUND;
            } else if (vt->osc_len < VT_MAX_OSC - 1) {
                vt->osc_buf[vt->osc_len++] = (char)(cp & 0xFF);
            }
            break;

        case VT_DCS_STRING:
            /* DCS sequences — ignore content, wait for ST */
            if (cp == '\x9C' || cp == '\a') vt->state = VT_GROUND;
            break;

        case VT_SS2:
        case VT_SS3:
            /* SS2/SS3 single-shift — just output char */
            screen_put_char(sb, cp, &sb->current_attr);
            vt->state = VT_GROUND;
            break;
    }
}

void vt_parser_feed(VtParser *vt, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        feed_utf8_byte(vt, (uint8_t)data[i]);
    }
}
