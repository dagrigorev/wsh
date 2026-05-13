/*
 * repl.c — Read-Eval-Print Loop implementation.
 *
 * Implements a ZSH-compatible readline buffer with:
 *   - Ctrl+A/E (BOL/EOL), Ctrl+K/U (kill), Ctrl+W (kill word)
 *   - Ctrl+R incremental history search
 *   - Tab completion with common-prefix insertion and menu on second Tab
 *   - Up/Down history navigation (arrow keys via ESC sequences)
 *   - Left/Right cursor movement
 *   - Multi-byte UTF-8 character insertion
 *   - precmd / preexec hook calls (ZSH compatibility)
 *
 * The IShellIO output path goes through ctx->io so the REPL is decoupled
 * from Win32 handles.  This makes it fully unit-testable.
 */
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "repl.h"
#include "shell/shell_ctx.h"
#include "shell/history.h"
#include "shell/completion.h"
#include "shell/builtins.h"
#include "shell/expand.h"
#include "core/str_util.h"
#include "core/log.h"
#include "core/unicode.h"

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/* Emit a string through ctx->io */
static void emit(Repl *r, const char *s) { io_write(r->ctx->io, s); }
static void emitln(Repl *r, const char *s) { io_writeln(r->ctx->io, s); }

static void emit_fmt(Repl *r, const char *fmt, ...) {
    char buf[512]; va_list ap;
    va_start(ap, fmt); _vsnprintf(buf, sizeof(buf)-1, fmt, ap); va_end(ap);
    emit(r, buf);
}

/* ── Lifecycle ─────────────────────────────────────────────────────────────── */

void repl_init(Repl *r, ShellContext *ctx) {
    memset(r, 0, sizeof(*r));
    r->ctx = ctx;
}

void repl_free(Repl *r) {
    completion_free(&r->completion);
}

/* ── Prompt ────────────────────────────────────────────────────────────────── */

void repl_show_prompt(Repl *r) {
    /* Call precmd hook if defined */
    for (ShellFunc *f = r->ctx->functions; f; f = f->next) {
        if (strcmp(f->name, "precmd") == 0) {
            shell_exec_line(r->ctx, "precmd");
            break;
        }
    }

    const char *fmt = shell_getenv(r->ctx, "PROMPT");
    if (!fmt) fmt = shell_getenv(r->ctx, "PS1");
    if (!fmt) fmt = "%~ %# ";   /* ZSH-like default */

    char *prompt = shell_expand_prompt(r->ctx, fmt);
    /* Emit directly — shell_expand_prompt has already handled % sequences.
     * Do NOT call expand_string here: it would strip backslashes in CWD paths. */
    if (prompt) {
        strncpy(r->prompt, prompt, sizeof(r->prompt) - 1);
        r->prompt[sizeof(r->prompt) - 1] = '\0';
        r->prompt_len = (int)strlen(r->prompt);
        r->prompt_cols = wsh_utf8_display_width_n(r->prompt, r->prompt_len);
        emit(r, prompt);
    } else {
        r->prompt[0] = '\0';
        r->prompt_len = 0;
        r->prompt_cols = 0;
    }
    str_free(prompt);
}

/* ── Line redraw (CR + erase line + reprint + reposition) ─────────────────── */

void repl_redraw_line(Repl *r) {
    /*
     * Sequence:
     *   \r          — carriage return (go to column 0)
     *   ESC[K       — erase to end of line
     *   <prompt>    — the current prompt
     *   <line_buf>  — current line content
     *   ESC[<n>D    — move cursor left by (len - cursor) columns
     */
    char seq[REPL_LINE_MAX + 64];
    int  n = 0;

    seq[n++] = '\r';
    seq[n++] = '\x1B'; seq[n++] = '['; seq[n++] = 'K'; /* erase EOL */

    if (r->prompt_len > 0) {
        int p_len = r->prompt_len;
        if (p_len > (int)sizeof(r->prompt) - 1) p_len = (int)sizeof(r->prompt) - 1;
        memcpy(seq + n, r->prompt, (size_t)p_len); n += p_len;
    }
    memcpy(seq + n, r->line, (size_t)r->len); n += r->len;

    int move_left = wsh_utf8_display_width_n(r->line + r->cursor, r->len - r->cursor);
    if (move_left > 0) {
        char mv[24];
        int ml = _snprintf(mv, sizeof(mv), "\x1B[%dD", move_left);
        memcpy(seq + n, mv, (size_t)ml); n += ml;
    }

    r->ctx->io->write(r->ctx->io, seq, n);
}

/* ── Insert character(s) at cursor ───────────────────────────────────────────*/

static void insert_bytes(Repl *r, const char *bytes, int len) {
    if (r->len + len >= REPL_LINE_MAX - 1) return;
    memmove(r->line + r->cursor + len,
            r->line + r->cursor,
            (size_t)(r->len - r->cursor));
    memcpy(r->line + r->cursor, bytes, (size_t)len);
    r->cursor += len;
    r->len    += len;
    repl_redraw_line(r);
}

/* ── Delete character before cursor ──────────────────────────────────────────*/

static void delete_backward(Repl *r) {
    if (r->cursor == 0) return;
    /* For multi-byte UTF-8: delete one character, not one byte. */
    int prev = wsh_utf8_prev_offset(r->line, r->cursor);
    int back = r->cursor - prev;
    memmove(r->line + r->cursor - back,
            r->line + r->cursor,
            (size_t)(r->len - r->cursor));
    r->cursor -= back;
    r->len    -= back;
    repl_redraw_line(r);
}

/* ── Tab completion ───────────────────────────────────────────────────────── */

static void handle_tab(Repl *r) {
    if (!r->completing) {
        /* First Tab: compute completions and insert common prefix */
        completion_free(&r->completion);
        r->line[r->len] = '\0';
        r->completion = completion_compute(r->line, r->cursor, r->ctx);

        if (r->completion.count == 0) return;

        if (r->completion.count == 1) {
            /* Unambiguous: replace word with the single match */
            r->cursor = completion_apply(&r->completion, 0,
                                         r->line, r->len, r->cursor,
                                         REPL_LINE_MAX - 1);
            r->len = (int)strlen(r->line);
            repl_redraw_line(r);
            completion_free(&r->completion);
            return;
        }

        /* Multiple matches: insert common prefix */
        int ws = r->cursor;
        while (ws > 0 && r->line[ws-1] != ' ' && r->line[ws-1] != '\t') ws--;

        int pfx_len = r->completion.common_prefix_len;
        const char *pfx = r->completion.matches[0];

        /* Replace word-prefix with common prefix */
        int old_word_len = r->cursor - ws;
        int delta        = pfx_len - old_word_len;
        if (r->len + delta < REPL_LINE_MAX - 1) {
            memmove(r->line + ws + pfx_len,
                    r->line + r->cursor,
                    (size_t)(r->len - r->cursor + 1));
            memcpy(r->line + ws, pfx, (size_t)pfx_len);
            r->len    += delta;
            r->cursor  = ws + pfx_len;
        }
        repl_redraw_line(r);
        r->completing = true;

    } else {
        /* Second Tab: show completion menu */
        emit(r, "\r\n");
        int cols = 0;
        for (int i = 0; i < r->completion.count && i < 40; i++) {
            emit_fmt(r, "%-24s", r->completion.matches[i]);
            if (++cols == 4) { emit(r, "\r\n"); cols = 0; }
        }
        if (cols > 0) emit(r, "\r\n");
        if (r->completion.count > 40)
            emit_fmt(r, "  ... %d more\r\n", r->completion.count - 40);
        repl_show_prompt(r);
        repl_redraw_line(r);
        r->completing = false;
        completion_free(&r->completion);
    }
}

/* ── Execute the current line ─────────────────────────────────────────────── */

static void execute_line(Repl *r) {
    emit(r, "\r\n");
    r->line[r->len] = '\0';

    /* Record non-empty lines in history */
    if (r->len > 0) {
        history_push(&r->ctx->history, r->line);

        /* preexec hook */
        for (ShellFunc *f = r->ctx->functions; f; f = f->next) {
            if (strcmp(f->name, "preexec") == 0) {
                shell_exec_line(r->ctx, "preexec");
                break;
            }
        }

        shell_exec_line(r->ctx, r->line);
    }

    /* Reset line state */
    r->len    = 0;
    r->cursor = 0;
    completion_free(&r->completion);
    r->completing = false;
}

/* ── Main input handler ───────────────────────────────────────────────────── */

bool repl_handle_input(Repl *r, const char *bytes, int len) {
    if (!bytes || len == 0) return true;

    /* Cancel any in-progress completion on non-Tab input */
    if (bytes[0] != '\t') { r->completing = false; }

    /* ── Single-byte control characters ─────────────────────────────────── */
    if (len == 1) {
        unsigned char c = (unsigned char)bytes[0];

        switch (c) {
            /* Ctrl+C — cancel line */
            case 0x03:
                emit(r, "^C\r\n");
                r->len = r->cursor = 0;
                completion_free(&r->completion);
                repl_show_prompt(r);
                return true;

            /* Ctrl+D — EOF (exit if line empty) */
            case 0x04:
                if (r->len == 0) {
                    emit(r, "exit\r\n");
                    return false; /* signal quit */
                }
                return true;

            /* Ctrl+L — clear screen */
            case 0x0C:
                emit(r, "\x1B[2J\x1B[H");
                repl_show_prompt(r);
                repl_redraw_line(r);
                return true;

            /* Ctrl+R — reverse history search */
            case 0x12:
                r->hist_search = true;
                r->hist_pat[0] = '\0';
                emit(r, "\r\n(reverse-i-search)`': ");
                return true;

            /* Enter */
            case '\r': case '\n':
                execute_line(r);
                if (r->ctx->exit_requested) return false;
                repl_show_prompt(r);
                return true;

            /* Tab */
            case '\t':
                handle_tab(r);
                return true;

            /* Backspace / DEL */
            case '\x7F': case '\b':
                delete_backward(r);
                return true;

            /* Ctrl+A — beginning of line */
            case 0x01:
                r->cursor = 0; repl_redraw_line(r); return true;

            /* Ctrl+E — end of line */
            case 0x05:
                r->cursor = r->len; repl_redraw_line(r); return true;

            /* Ctrl+K — kill to EOL */
            case 0x0B:
                r->len = r->cursor; repl_redraw_line(r); return true;

            /* Ctrl+U — kill to BOL */
            case 0x15:
                memmove(r->line, r->line + r->cursor,
                        (size_t)(r->len - r->cursor + 1));
                r->len -= r->cursor; r->cursor = 0;
                repl_redraw_line(r); return true;

            /* Ctrl+W — kill word before cursor */
            case 0x17: {
                int end = r->cursor;
                while (r->cursor > 0 && r->line[wsh_utf8_prev_offset(r->line, r->cursor)] == ' ')
                r->cursor = wsh_utf8_prev_offset(r->line, r->cursor);
                while (r->cursor > 0 && r->line[wsh_utf8_prev_offset(r->line, r->cursor)] != ' ')
                    r->cursor = wsh_utf8_prev_offset(r->line, r->cursor);
                memmove(r->line + r->cursor, r->line + end,
                        (size_t)(r->len - end + 1));
                r->len -= (end - r->cursor);
                repl_redraw_line(r); return true;
            }

            /* Ctrl+Y — yank (stub: would need a kill ring) */
            case 0x19: return true;

            default:
                /* Printable ASCII */
                if (c >= 0x20) {
                    char ch = (char)c;
                    insert_bytes(r, &ch, 1);
                }
                return true;
        }
    }

    /* ── Escape sequences (arrow keys, Home, End, etc.) ─────────────────── */
    if (len >= 3 && bytes[0] == '\x1B' && bytes[1] == '[') {
        switch (bytes[2]) {
            case 'A': { /* Up — history prev */
                const char *prev = history_prev(&r->ctx->history);
                if (prev) {
                    strncpy(r->line, prev, REPL_LINE_MAX - 1);
                    r->len = r->cursor = (int)strlen(r->line);
                    repl_redraw_line(r);
                }
                return true;
            }
            case 'B': { /* Down — history next */
                const char *nxt = history_next(&r->ctx->history);
                r->line[0] = '\0'; r->len = r->cursor = 0;
                if (nxt) {
                    strncpy(r->line, nxt, REPL_LINE_MAX - 1);
                    r->len = r->cursor = (int)strlen(r->line);
                }
                repl_redraw_line(r); return true;
            }
            case 'C': /* Right */
                if (r->cursor < r->len) {
                    r->cursor = wsh_utf8_next_offset(r->line, r->len, r->cursor);
                    repl_redraw_line(r);
                }
                return true;
            case 'D': /* Left */
                if (r->cursor > 0) {
                    r->cursor = wsh_utf8_prev_offset(r->line, r->cursor);
                    repl_redraw_line(r);
                }
                return true;
            case 'H': /* Home */
                r->cursor = 0; repl_redraw_line(r); return true;
            case 'F': /* End */
                r->cursor = r->len; repl_redraw_line(r); return true;
            case '3': /* Del — ESC [ 3 ~ */
                if (len >= 4 && bytes[3] == '~') {
                    if (r->cursor < r->len) {
                        int next = wsh_utf8_next_offset(r->line, r->len, r->cursor);
                        int fwd = next - r->cursor;
                        memmove(r->line + r->cursor,
                                r->line + r->cursor + fwd,
                                (size_t)(r->len - r->cursor - fwd + 1));
                        r->len -= fwd;
                        repl_redraw_line(r);
                    }
                }
                return true;
            default: break;
        }
    }

    /* ── Ctrl+Left / Ctrl+Right (word movement, ESC [ 1 ; 5 D/C) ─────────── */
    if (len >= 6 && bytes[0] == '\x1B' && bytes[1] == '[' &&
        bytes[2] == '1' && bytes[3] == ';' && bytes[4] == '5') {
        if (bytes[5] == 'D') { /* Ctrl+Left — back word */
            while (r->cursor > 0 && r->line[wsh_utf8_prev_offset(r->line, r->cursor)] == ' ')
                r->cursor = wsh_utf8_prev_offset(r->line, r->cursor);
            while (r->cursor > 0 && r->line[wsh_utf8_prev_offset(r->line, r->cursor)] != ' ')
                r->cursor = wsh_utf8_prev_offset(r->line, r->cursor);
            repl_redraw_line(r);
        } else if (bytes[5] == 'C') { /* Ctrl+Right — forward word */
            while (r->cursor < r->len && r->line[r->cursor] == ' ')
                r->cursor = wsh_utf8_next_offset(r->line, r->len, r->cursor);
            while (r->cursor < r->len && r->line[r->cursor] != ' ')
                r->cursor = wsh_utf8_next_offset(r->line, r->len, r->cursor);
            repl_redraw_line(r);
        }
        return true;
    }

    /* ── Multi-byte UTF-8 character ──────────────────────────────────────── */
    if ((unsigned char)bytes[0] >= 0x80) {
        insert_bytes(r, bytes, len);
        return true;
    }

    return true;
}
