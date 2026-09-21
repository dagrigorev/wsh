/* Transliterated from Ghostty src/terminal/osc.zig, src/terminal/osc/
 * encoding.zig and these files under src/terminal/osc/parsers/:
 * change_window_title.zig, change_window_icon.zig, hyperlink.zig,
 * report_pwd.zig, mouse_shape.zig, clipboard_operation.zig, color.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * OSC (Operating System Command) related functions and types.
 *
 * OSC is another set of control sequences for terminal programs that start
 * with "ESC ]". Unlike CSI or standard ESC sequences, they may contain strings
 * and other irregular formatting so a dedicated parser is created to handle
 * it.
 *
 * TRANSLITERATION, see parser.hpp for the Zig-to-C++ mapping. Additionally:
 *
 *   ?Allocator          a flag; allocation is malloc/realloc/free
 *   [:0]const u8        ZStr, a pointer and length whose byte at len is NUL
 *   std.Io.Writer       Writer, which models the fixed and allocating writers
 *                       closely enough that upstream's tests on buffer
 *                       capacity hold exactly
 *
 * Comments are upstream's unless marked "Wisp:".
 *
 * Wisp: PARSERS NOT YET TRANSLITERATED. osc/parsers/ holds one file per
 * command family. Only the ones named above are here; the rest — osc9,
 * kitty_color, kitty_text_sizing,
 * kitty_clipboard_protocol, kitty_dnd_protocol, kitty_desktop_notification,
 * context_signal, semantic_prompt, rxvt_extension and iterm2 — arrive in
 * later slices. Until then end() returns null for their states, exactly as
 * it would for an invalid sequence, and Command keeps a tag for each so the
 * dispatch in end() already has upstream's shape.
 */

#pragma once
#ifndef WISP_TERMINAL_OSC_HPP
#define WISP_TERMINAL_OSC_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "color.hpp"
#include "../datastruct/segmented_list.hpp"

namespace wisp {
namespace terminal {
namespace osc {

/* ─── encoding.zig ───────────────────────────────────────────────────────── */

/* Wisp: std.unicode.Utf8View.init — strict UTF-8 validation. Rejects
 * overlong encodings, surrogates and anything above U+10FFFF. On success
 * calls back once per codepoint. */
template <typename F>
inline bool utf8_view_each(const uint8_t *s, size_t len, F f) {
    size_t i = 0;
    while (i < len) {
        const uint8_t b = s[i];
        uint32_t cp;
        size_t n;
        uint32_t min;

        if (b < 0x80) { cp = b; n = 1; min = 0; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; n = 2; min = 0x80; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; n = 3; min = 0x800; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; n = 4; min = 0x10000; }
        else return false;

        if (i + n > len) return false;
        for (size_t k = 1; k < n; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if (cp < min) return false;                        /* overlong */
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;    /* surrogate */
        if (cp > 0x10FFFF) return false;

        if (!f(cp)) return false;
        i += n;
    }
    return true;
}

/* Kitty defines "Escape code safe UTF-8" as valid UTF-8 with the
 * additional requirement of not containing any C0 escape codes
 * (0x00-0x1f), DEL (0x7f) and C1 escape codes (0x80-0x9f).
 *
 * Used by OSC 66 (text sizing) and OSC 99 (Kitty notifications).
 *
 * See: https://sw.kovidgoyal.net/kitty/desktop-notifications/#safe-utf8 */
inline bool isSafeUtf8(const char *s, size_t len) {
    return utf8_view_each((const uint8_t *)s, len, [](uint32_t cp) {
        if (cp <= 0x1f || cp == 0x7f || (cp >= 0x80 && cp <= 0x9f)) {
            return false;
        }
        return true;
    });
}

inline bool isSafeUtf8(const char *s) { return isSafeUtf8(s, strlen(s)); }

/* ─── osc.zig ────────────────────────────────────────────────────────────── */

/* Wisp: [:0]const u8. */
struct ZStr {
    const char *ptr;
    size_t      len;

    ZStr() : ptr(""), len(0) {}
    ZStr(const char *p, size_t n) : ptr(p), len(n) {}

    bool eql(const char *s) const {
        return strlen(s) == len && memcmp(ptr, s, len) == 0;
    }
};

/* The terminator used to end an OSC command. For OSC commands that demand
 * a response, we try to match the terminator used in the request since that
 * is most likely to be accepted by the calling program. */
enum class Terminator : uint8_t {
    /* The preferred string terminator is ESC followed by \ */
    st,

    /* Some applications and terminals use BELL (0x07) as the string
     * terminator. */
    bel,
};

/* Initialize the terminator based on the last byte seen. If the
 * last byte is a BEL then we use BEL, otherwise we just assume ST.
 *
 * Wisp: ?u8 is has_ch plus ch. */
inline Terminator terminator_init(bool has_ch, uint8_t ch) {
    if (!has_ch) return Terminator::st;
    return ch == 0x07 ? Terminator::bel : Terminator::st;
}

/* The terminator as a string. This is static memory so it doesn't
 * need to be freed. */
inline const char *terminator_string(Terminator t) {
    return t == Terminator::st ? "\x1b\\" : "\x07";
}

/* ─── osc/parsers/color.zig: types ──────────────────────────────────────── */

namespace color {

/* The possible operations we support for colors. */
enum class Operation : uint8_t {
    osc_4,
    osc_5,
    osc_10,
    osc_11,
    osc_12,
    osc_13,
    osc_14,
    osc_15,
    osc_16,
    osc_17,
    osc_18,
    osc_19,
    osc_104,
    osc_105,
    osc_110,
    osc_111,
    osc_112,
    osc_113,
    osc_114,
    osc_115,
    osc_116,
    osc_117,
    osc_118,
    osc_119,
};

struct Target {
    enum class Tag : uint8_t { palette, special, dynamic };
    Tag tag;
    uint8_t palette;
    ::wisp::terminal::Special special;
    ::wisp::terminal::Dynamic dynamic;

    static Target makePalette(uint8_t idx) {
        Target t = Target();
        t.tag = Tag::palette;
        t.palette = idx;
        return t;
    }
    static Target makeSpecial(::wisp::terminal::Special sp) {
        Target t = Target();
        t.tag = Tag::special;
        t.special = sp;
        return t;
    }
    static Target makeDynamic(::wisp::terminal::Dynamic d) {
        Target t = Target();
        t.tag = Tag::dynamic;
        t.dynamic = d;
        return t;
    }

    bool eql(const Target &o) const {
        if (tag != o.tag) return false;
        switch (tag) {
            case Tag::palette: return palette == o.palette;
            case Tag::special: return special == o.special;
            case Tag::dynamic: return dynamic == o.dynamic;
        }
        return false;
    }

    Target()
        : tag(Tag::palette), palette(0),
          special(::wisp::terminal::Special::bold),
          dynamic(::wisp::terminal::Dynamic::foreground) {}
};

struct ColoredTarget {
    Target target;
    ::wisp::terminal::RGB color;
};

/* A single operation related to the terminal color palette. */
struct Request {
    enum class Tag : uint8_t { set, query, reset, reset_palette, reset_special };
    Tag tag;
    ColoredTarget set;
    Target query;
    Target reset;

    Request() : tag(Tag::reset_palette), set(), query(), reset() {}

    bool eql(const Request &o) const {
        if (tag != o.tag) return false;
        switch (tag) {
            case Tag::set:
                return set.target.eql(o.set.target) && set.color.eql(o.set.color);
            case Tag::query: return query.eql(o.query);
            case Tag::reset: return reset.eql(o.reset);
            case Tag::reset_palette:
            case Tag::reset_special:
                return true;
        }
        return false;
    }
};

/* A segmented list is used to avoid copying when many operations
 * are given in a single OSC. In most cases, OSC 4/104/etc. send
 * very few so the prealloc is optimized for that.
 *
 * The exact prealloc value is chosen arbitrarily assuming most
 * color ops have very few. If we can get empirical data on more
 * typical values we can switch to that. */
typedef ::wisp::datastruct::SegmentedList<Request, 2> List;

} /* namespace color */

struct Command {
    /* NOTE: Order matters, see LibEnum documentation. */
    enum class Key : uint8_t {
        invalid,
        change_window_title,
        change_window_icon,
        semantic_prompt,
        clipboard_contents,
        report_pwd,
        mouse_shape,
        color_operation,
        kitty_color_protocol,
        show_desktop_notification,
        hyperlink_start,
        hyperlink_end,
        conemu_sleep,
        conemu_show_message_box,
        conemu_change_tab_title,
        conemu_progress_report,
        conemu_wait_input,
        conemu_guimacro,
        conemu_run_process,
        conemu_output_environment_variable,
        conemu_xterm_emulation,
        conemu_comment,
        kitty_text_sizing,
        kitty_clipboard_protocol,
        kitty_dnd_protocol,
        context_signal,
        kitty_desktop_notification,
    };

    Key key;

    /* Set the window title of the terminal
     *
     * If title mode 0 is set text is expect to be hex encoded (i.e. utf-8
     * with each code unit further encoded with two hex digits).
     *
     * If title mode 2 is set or the terminal is setup for unconditional
     * utf-8 titles text is interpreted as utf-8. Else text is interpreted
     * as latin1. */
    ZStr change_window_title;

    /* Set the icon of the terminal window. The name of the icon is not
     * well defined, so this is currently ignored by Ghostty at the time
     * of writing this. We just parse it so that we don't get parse errors
     * in the log. */
    ZStr change_window_icon;

    /* OSC 7. Reports the current working directory of the shell. This is
     * a moderately flawed escape sequence but one that many major terminals
     * support so we also support it. To understand the flaws, read through
     * this terminal-wg issue:
     * https://gitlab.freedesktop.org/terminal-wg/specifications/-/issues/20 */
    struct {
        /* The reported pwd value. This is not checked for validity. It
         * should be a file URL but it is up to the caller to utilize this
         * value. */
        ZStr value;
    } report_pwd;

    /* OSC 22. Set the mouse shape. There doesn't seem to be a standard
     * naming scheme for cursors but it looks like terminals such as Foot
     * are moving towards using the W3C CSS cursor names. For OSC parsing,
     * we just parse whatever string is given. */
    struct {
        ZStr value;
    } mouse_shape;

    /* Set or get clipboard contents. If data is "?", then the current
     * clipboard contents are sent to the pty. Otherwise, the contents
     * are set on the clipboard. */
    struct {
        uint8_t    kind;
        ZStr       data;
        Terminator terminator;   /* = .st */
    } clipboard_contents;

    /* OSC color operations to set, reset, or report color settings. Some
     * OSCs allow multiple operations to be specified in a single OSC so we
     * need a list-like datastructure to manage them. We use
     * std.SegmentedList because it minimizes the number of allocations and
     * copies because a large majority of the time there will be only one
     * operation per OSC.
     *
     * Currently, these OSCs are handled by `color_operation`:
     *
     * 4, 5, 10-19, 104, 105, 110-119
     *
     * Wisp: the list is owned by the Command and freed by Parser::reset, as
     * upstream's is. A Command copied out of the parser is a shallow copy, as
     * a Zig struct copy is, and is valid until the parser's next reset. */
    struct {
        color::Operation op;
        color::List      requests;
        Terminator       terminator;   /* = .st */
    } color_operation;

    /* Kitty's drag and drop protocol (OSC 72), osc/parsers/kitty_dnd_protocol.zig
     * OSC. Only the raw metadata and payload are captured. */
    struct {
        /* The raw metadata that was received. */
        ZStr metadata;
        /* The raw payload. Null (has_payload false) when the OSC had no
         * `;` after the metadata; an empty payload is distinct from no
         * payload. */
        bool has_payload;
        ZStr payload;
        /* The terminator used for this OSC, so any response can match it. */
        Terminator terminator;
    } kitty_dnd_protocol;

    /* Show a desktop notification (OSC 9 or OSC 777) */
    struct {
        ZStr title;
        ZStr body;
    } show_desktop_notification;

    /* Start a hyperlink (OSC 8) */
    struct {
        bool has_id;   /* Wisp: id: ?[:0]const u8 = null */
        ZStr id;
        ZStr uri;
    } hyperlink_start;

    Command() : key(Key::invalid), change_window_title(), change_window_icon() {
        report_pwd.value = ZStr();
        mouse_shape.value = ZStr();
        clipboard_contents.kind = 0;
        clipboard_contents.data = ZStr();
        clipboard_contents.terminator = Terminator::st;
        color_operation.op = color::Operation::osc_4;
        color_operation.terminator = Terminator::st;
        hyperlink_start.has_id = false;
        hyperlink_start.id = ZStr();
        hyperlink_start.uri = ZStr();
    }
};

/* Wisp: std.Io.Writer, restricted to the two backings osc.zig uses. For the
 * allocating backing, `capacity` is what upstream's tests call
 * writer.buffer.len. */
struct Writer {
    bool   allocating;
    char  *buf;
    size_t capacity;
    size_t end;

    Writer() : allocating(false), buf(nullptr), capacity(0), end(0) {}

    size_t buffered_len() const { return end; }

    /* Wisp: writer.writeByte on the underlying writer. A fixed writer fails
     * when full. An allocating one grows as std.Io.Writer.Allocating does —
     * which is not bounded by the capture's max_bytes, and that is why
     * upstream's parsers can fail on their NUL terminator in one backing and
     * not the other. */
    bool writeByte(uint8_t b) {
        if (end >= capacity) {
            if (!allocating) return false;
            const size_t want = capacity ? capacity * 2 : 1;
            char *p = (char *)realloc(buf, want);
            if (!p) return false;
            buf = p;
            capacity = want;
        }
        buf[end++] = (char)b;
        return true;
    }

    bool writeAll(const uint8_t *bytes, size_t n) {
        for (size_t i = 0; i < n; i++) {
            if (!writeByte(bytes[i])) return false;
        }
        return true;
    }
};

struct Parser {
    /* Maximum size of a "normal" OSC. */
    static const size_t MAX_BUF = 2048;

    /* Maximum size of an OSC that requires dynamically allocated storage.
     * OSC input is untrusted, so these captures must have a finite bound. */
    static const size_t MAX_ALLOCATING_BUF = 8 * 1024 * 1024;

    enum class State : uint8_t {
        start,
        invalid,

        /* OSC command prefixes. Not all of these are valid OSCs, but may be
         * needed to "bridge" to a valid OSC (e.g. to support OSC 777 we need
         * to have a state "77" even though there is no OSC 77). */
        s0, s1, s2, s3, s4, s5, s6, s7, s8, s9,
        s30, s300, s3008,
        s10, s11, s12, s13, s14, s15, s16, s17, s18, s19,
        s21, s22,
        s52, s55, s66, s72, s77, s99,
        s104,
        s110, s111, s112, s113, s114, s115, s116, s117, s118, s119,
        s133, s552, s777, s1337, s5522,
    };

    struct Capture {
        enum class Mode : uint8_t { fixed, allocating };

        Writer writer;
        size_t max_bytes;

        static void fixed(Capture *c, char *buf, size_t len) {
            c->writer = Writer();
            c->writer.allocating = false;
            c->writer.buf = buf;
            c->writer.capacity = len;
            c->max_bytes = len;
        }

        static bool allocating(Capture *c, size_t max_bytes) {
            c->writer = Writer();
            c->writer.allocating = true;
            const size_t initial = MAX_BUF < max_bytes ? MAX_BUF : max_bytes;
            c->writer.buf = (char *)malloc(initial ? initial : 1);
            if (!c->writer.buf) return false;
            c->writer.capacity = initial;
            c->max_bytes = max_bytes;
            return true;
        }

        /* Append one byte without permitting the backing allocation to grow
         * beyond max_bytes. Allocating.Writer normally grows super-linearly,
         * so grow it explicitly to keep the allocation itself bounded too. */
        bool writeByte(uint8_t byte) {
            if (writer.buffered_len() >= max_bytes) return false;

            if (writer.allocating && writer.end >= writer.capacity) {
                size_t doubled = writer.capacity * 2;
                if (doubled < writer.capacity) doubled = (size_t)-1;  /* *| */
                const size_t grown = doubled > 1 ? doubled : 1;
                const size_t new_capacity = grown < max_bytes ? grown : max_bytes;
                char *p = (char *)realloc(writer.buf, new_capacity);
                if (!p) return false;
                writer.buf = p;
                writer.capacity = new_capacity;
            }

            return writer.writeByte(byte);
        }

        /* Append a slice without permitting the backing allocation to
         * grow beyond max_bytes. This matches the byte-at-a-time
         * semantics of writeByte: bytes are retained up to exactly
         * max_bytes and the first byte that doesn't fit fails the
         * write. */
        bool writeSlice(const uint8_t *bytes, size_t len) {
            const size_t avail = max_bytes - writer.buffered_len();
            const size_t n = len < avail ? len : avail;

            if (writer.allocating) {
                const size_t needed = writer.end + n;
                if (needed > writer.capacity) {
                    size_t doubled = writer.capacity * 2;
                    if (doubled < writer.capacity) doubled = (size_t)-1;
                    const size_t grown = doubled > needed ? doubled : needed;
                    const size_t new_capacity =
                        grown < max_bytes ? grown : max_bytes;
                    char *p = (char *)realloc(writer.buf, new_capacity);
                    if (!p) return false;
                    writer.buf = p;
                    writer.capacity = new_capacity;
                }
            }

            if (!writer.writeAll(bytes, n)) return false;
            if (n < len) return false;
            return true;
        }

        void deinit() {
            if (writer.allocating) {
                free(writer.buf);
                writer.buf = nullptr;
                writer.capacity = 0;
            }
        }

        /* Return the captured trailing data. This is the data from the
         * point that trailing data capture was requested. */
        char *trailing() { return writer.buf; }
        size_t trailing_len() const { return writer.end; }
    };

    /* Optional allocator used to accept data longer than MAX_BUF.
     * This only applies to some commands (e.g. OSC 52) that can
     * reasonably exceed MAX_BUF.
     *
     * Wisp: ?Allocator — whether one was provided. */
    bool alloc;

    /* Maximum number of bytes retained by an allocating capture.
     * This is configurable primarily so callers and tests can choose a
     * smaller policy than the default. */
    size_t max_allocating_bytes;

    /* Current state of the parser. */
    State state;

    /* Buffer for temporary storage of OSC data */
    char buffer[MAX_BUF];

    /* Capture state. If this is set then we're actively capturing the
     * bytes coming into the parser.
     *
     * Wisp: ?Capture is has_capture plus capture. */
    bool    has_capture;
    Capture capture;

    /* The command that is the result of parsing. */
    Command command;

    explicit Parser(bool with_alloc = false)
        : alloc(with_alloc), max_allocating_bytes(MAX_ALLOCATING_BUF),
          state(State::start), has_capture(false), capture(), command() {}

    ~Parser() { deinit(); }

    Parser(const Parser &) = delete;
    Parser &operator=(const Parser &) = delete;

    /* This must be called to clean up any allocated memory. */
    void deinit() { reset(); }

    /* Reset the parser state. */
    void reset() {
        /* If we're capturing, then stop it. */
        if (has_capture) capture.deinit();

        /* Handle any cleanup that individual OSCs require.
         * Wisp: kitty_color_protocol also owns an allocation upstream; it is
         * not transliterated yet. */
        if (command.key == Command::Key::color_operation && alloc) {
            command.color_operation.requests.deinit();
        }

        state = State::start;
        has_capture = false;
        command = Command();
    }

    /* Make sure that we have an allocator. If we don't, set the state to
     * invalid so that any additional OSC data is discarded. */
    bool ensureAllocator() {
        if (alloc) return true;
        /* log.warn("An allocator is required to process OSC ...") */
        state = State::invalid;
        return false;
    }

    /* Begin capturing trailing data. All inputs to next from this point
     * forward will be captured into the `self.capture.writer` buffer
     * which may be backed by either a fixed size or allocating buffer
     * depending on mode.
     *
     * Get the trailing data using `capture.trailing()`. Do not access
     * the writer directly. */
    void captureTrailing(Capture::Mode mode) {
        switch (mode) {
            case Capture::Mode::fixed:
                Capture::fixed(&capture, buffer, MAX_BUF);
                has_capture = true;
                return;

            case Capture::Mode::allocating:
                if (!alloc) {
                    /* We don't have an allocator - fall back to a fixed
                     * buffer and hope that it's big enough. */
                    captureTrailing(Capture::Mode::fixed);
                    return;
                }

                if (!Capture::allocating(&capture, max_allocating_bytes)) {
                    /* The allocator failed for some reason, fall back to a
                     * fixed buffer and hope that it's big enough. */
                    captureTrailing(Capture::Mode::fixed);
                    return;
                }
                has_capture = true;
                return;
        }
    }

    /* Consume a slice of bytes, advancing the parser state. This is
     * equivalent to calling `next` for each byte in order, but is much
     * faster once a data capture is active because the remaining bytes
     * are appended to the capture in bulk. */
    void nextSlice(const uint8_t *input, size_t len) {
        if (state == State::invalid) return;

        /* Run the state machine byte-at-a-time until a capture begins.
         * The command prefix before a capture starts is only a handful
         * of bytes so this loop is short in practice. */
        size_t offset = 0;
        while (!has_capture) {
            if (offset >= len) return;
            next(input[offset]);
            offset += 1;
            if (state == State::invalid) return;
        }

        const size_t rem = len - offset;
        if (rem == 0) return;
        if (!capture.writeSlice(input + offset, rem)) {
            /* We have overflowed our buffer or had some other error, set
             * the state to invalid so that we discard any further input. */
            state = State::invalid;
        }
    }

    void nextSlice(const char *s) { nextSlice((const uint8_t *)s, strlen(s)); }

    /* Consume the next character c and advance the parser state. */
    void next(uint8_t c);

    /* End the sequence and return the command, if any. If the return value
     * is null, then no valid command was found. The optional terminator_ch
     * is the final character in the OSC sequence. This is used to determine
     * the response terminator.
     *
     * The returned pointer is only valid until the next call to the parser.
     * Callers should copy out any data they wish to retain across calls.
     *
     * Wisp: ?u8 is has_ch plus ch. */
    Command *end(bool has_ch, uint8_t ch);
    Command *end() { return end(false, 0); }
    Command *end(uint8_t ch) { return end(true, ch); }
};

inline void Parser::next(uint8_t c) {
    typedef State S;
    typedef Capture::Mode M;

    /* If the state becomes invalid for any reason, just discard
     * any further input. */
    if (state == S::invalid) return;

    /* If a writer has been initialized, we just accumulate the rest of the
     * OSC sequence in the writer's buffer and skip the state machine. */
    if (has_capture) {
        if (!capture.writeByte(c)) {
            /* We have overflowed our buffer or had some other error, set the
             * state to invalid so that we discard any further input. */
            state = S::invalid;
        }
        return;
    }

    switch (state) {
        /* handled above, so should never be here */
        case S::invalid:
            return;

        case S::start:
            switch (c) {
                case '0': state = S::s0; break;
                case '1': state = S::s1; break;
                case '2': state = S::s2; break;
                case '3': state = S::s3; break;
                case '4': state = S::s4; break;
                case '5': state = S::s5; break;
                case '6': state = S::s6; break;
                case '7': state = S::s7; break;
                case '8': state = S::s8; break;
                case '9': state = S::s9; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s3:
            if (c == '0') state = S::s30; else state = S::invalid;
            return;

        case S::s30:
            if (c == '0') state = S::s300; else state = S::invalid;
            return;

        case S::s300:
            if (c == '8') state = S::s3008; else state = S::invalid;
            return;

        case S::s3008:
            if (c == ';') captureTrailing(M::fixed); else state = S::invalid;
            return;

        case S::s1:
            switch (c) {
                case ';': captureTrailing(M::fixed); break;
                case '0': state = S::s10; break;
                case '1': state = S::s11; break;
                case '2': state = S::s12; break;
                case '3': state = S::s13; break;
                case '4': state = S::s14; break;
                case '5': state = S::s15; break;
                case '6': state = S::s16; break;
                case '7': state = S::s17; break;
                case '8': state = S::s18; break;
                case '9': state = S::s19; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s10:
            switch (c) {
                case ';': if (ensureAllocator()) captureTrailing(M::fixed); break;
                case '4': state = S::s104; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s104:
            if (c == ';') {
                if (ensureAllocator()) captureTrailing(M::fixed);
            } else {
                state = S::invalid;
            }
            return;

        case S::s11:
            switch (c) {
                case ';': if (ensureAllocator()) captureTrailing(M::fixed); break;
                case '0': state = S::s110; break;
                case '1': state = S::s111; break;
                case '2': state = S::s112; break;
                case '3': state = S::s113; break;
                case '4': state = S::s114; break;
                case '5': state = S::s115; break;
                case '6': state = S::s116; break;
                case '7': state = S::s117; break;
                case '8': state = S::s118; break;
                case '9': state = S::s119; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s4:
        case S::s12:
        case S::s14:
        case S::s15:
        case S::s16:
        case S::s17:
        case S::s18:
        case S::s19:
        case S::s21:
        case S::s110:
        case S::s111:
        case S::s112:
        case S::s113:
        case S::s114:
        case S::s115:
        case S::s116:
        case S::s117:
        case S::s118:
        case S::s119:
            if (c == ';') {
                if (ensureAllocator()) captureTrailing(M::fixed);
            } else {
                state = S::invalid;
            }
            return;

        case S::s13:
            switch (c) {
                case ';': if (ensureAllocator()) captureTrailing(M::fixed); break;
                case '3': state = S::s133; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s2:
            switch (c) {
                case ';': captureTrailing(M::fixed); break;
                case '1': state = S::s21; break;
                case '2': state = S::s22; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s5:
            switch (c) {
                case ';': if (ensureAllocator()) captureTrailing(M::fixed); break;
                case '2': state = S::s52; break;
                case '5': state = S::s55; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s6:
            if (c == '6') state = S::s66; else state = S::invalid;
            return;

        case S::s52:
        case S::s66:
            if (c == ';') captureTrailing(M::allocating); else state = S::invalid;
            return;

        case S::s55:
            if (c == '2') state = S::s552; else state = S::invalid;
            return;

        case S::s7:
            switch (c) {
                case ';': captureTrailing(M::fixed); break;
                case '2': state = S::s72; break;
                case '7': state = S::s77; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s72:
            if (c == ';') captureTrailing(M::allocating); else state = S::invalid;
            return;

        case S::s77:
            if (c == '7') state = S::s777; else state = S::invalid;
            return;

        case S::s133:
            switch (c) {
                case ';': captureTrailing(M::fixed); break;
                case '7': state = S::s1337; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s552:
            if (c == '2') state = S::s5522; else state = S::invalid;
            return;

        case S::s1337:
            if (c == ';') captureTrailing(M::fixed); else state = S::invalid;
            return;

        case S::s5522:
            if (c == ';') captureTrailing(M::allocating); else state = S::invalid;
            return;

        case S::s9:
            switch (c) {
                case ';': captureTrailing(M::fixed); break;
                case '9': state = S::s99; break;
                default: state = S::invalid; break;
            }
            return;

        case S::s99:
            /* OSC 99 encoded payloads can exceed the fixed buffer. */
            if (c == ';') captureTrailing(M::allocating); else state = S::invalid;
            return;

        case S::s0:
        case S::s22:
        case S::s777:
        case S::s8:
            if (c == ';') captureTrailing(M::fixed); else state = S::invalid;
            return;
    }
}

/* ─── the osc parsers ──────────────────────────────────────────────────── */

namespace parsers {

/* The four simple parsers share this shape upstream, each written out in
 * its own file. Wisp: kept as one helper plus four callers, since the files
 * differ only in which field receives the string. */
inline bool take_nul_terminated(Parser *parser, ZStr *out) {
    if (!parser->has_capture) {
        parser->state = Parser::State::invalid;
        return false;
    }
    Parser::Capture *cap = &parser->capture;
    if (!cap->writer.writeByte(0)) {
        parser->state = Parser::State::invalid;
        return false;
    }
    char *data = cap->trailing();
    const size_t len = cap->trailing_len();
    *out = ZStr(data, len - 1);
    return true;
}

namespace change_window_title {
/* Parse OSC 0 and OSC 2 */
inline Command *parse(Parser *parser, bool, uint8_t) {
    ZStr s;
    if (!take_nul_terminated(parser, &s)) return nullptr;
    parser->command = Command();
    parser->command.key = Command::Key::change_window_title;
    parser->command.change_window_title = s;
    return &parser->command;
}
} /* namespace change_window_title */

namespace change_window_icon {
/* Parse OSC 1 */
inline Command *parse(Parser *parser, bool, uint8_t) {
    ZStr s;
    if (!take_nul_terminated(parser, &s)) return nullptr;
    parser->command = Command();
    parser->command.key = Command::Key::change_window_icon;
    parser->command.change_window_icon = s;
    return &parser->command;
}
} /* namespace change_window_icon */

namespace report_pwd {
/* Parse OSC 7 */
inline Command *parse(Parser *parser, bool, uint8_t) {
    ZStr s;
    if (!take_nul_terminated(parser, &s)) return nullptr;
    parser->command = Command();
    parser->command.key = Command::Key::report_pwd;
    parser->command.report_pwd.value = s;
    return &parser->command;
}
} /* namespace report_pwd */

namespace mouse_shape {
/* Parse OSC 22 */
inline Command *parse(Parser *parser, bool, uint8_t) {
    /* assert(parser.state == .@"22") */
    ZStr s;
    if (!take_nul_terminated(parser, &s)) return nullptr;
    parser->command = Command();
    parser->command.key = Command::Key::mouse_shape;
    parser->command.mouse_shape.value = s;
    return &parser->command;
}
} /* namespace mouse_shape */

namespace hyperlink {
/* Parse OSC 8 hyperlinks */
inline Command *parse(Parser *parser, bool, uint8_t) {
    if (!parser->has_capture) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    Parser::Capture *cap = &parser->capture;
    if (!cap->writer.writeByte(0)) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    char *data = cap->trailing();
    const size_t data_len = cap->trailing_len();

    const char *semi = (const char *)memchr(data, ';', data_len);
    if (!semi) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    const size_t s = (size_t)(semi - data);

    parser->command = Command();
    parser->command.key = Command::Key::hyperlink_start;
    parser->command.hyperlink_start.uri = ZStr(data + s + 1, data_len - 1 - (s + 1));

    data[s] = 0;
    char *kvs = data;
    const size_t kvs_len = s + 1;
    for (size_t i = 0; i < kvs_len; i++) {
        if (kvs[i] == ':') kvs[i] = 0;
    }

    size_t kv_start = 0;
    while (kv_start < kvs_len) {
        /* std.mem.indexOfScalarPos(u8, kvs, kv_start + 1, 0) */
        size_t kv_end = (size_t)-1;
        for (size_t i = kv_start + 1; i < kvs_len; i++) {
            if (kvs[i] == 0) {
                kv_end = i;
                break;
            }
        }
        if (kv_end == (size_t)-1) break;

        char *kv = data + kv_start;
        const size_t kv_len = kv_end + 1 - kv_start;
        const char *eq = (const char *)memchr(kv, '=', kv_len);
        if (!eq) break;
        const size_t v = (size_t)(eq - kv);

        const size_t key_len = v;
        const ZStr value(kv + v + 1, kv_len - 1 - (v + 1));
        if (key_len == 2 && memcmp(kv, "id", 2) == 0) {
            if (value.len > 0) {
                parser->command.hyperlink_start.has_id = true;
                parser->command.hyperlink_start.id = value;
            }
        } else {
            /* log.warn("unknown hyperlink option: '{s}'") */
        }
        kv_start = kv_end + 1;
    }

    if (parser->command.hyperlink_start.uri.len == 0) {
        if (parser->command.hyperlink_start.has_id) {
            parser->state = Parser::State::invalid;
            return nullptr;
        }
        parser->command = Command();
        parser->command.key = Command::Key::hyperlink_end;
    }

    return &parser->command;
}
} /* namespace hyperlink */

namespace clipboard_operation {
/* Parse OSC 52 */
inline Command *parse(Parser *parser, bool has_ch, uint8_t terminator_ch) {
    /* assert(parser.state == .@"52") */
    if (!parser->has_capture) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    Parser::Capture *cap = &parser->capture;

    /* Wisp: unlike the parsers above, this one writes its terminator
     * through the bounded Capture.writeByte, so a capture already at its
     * limit fails here. Upstream tests exactly that. */
    if (!cap->writeByte(0)) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    char *data = cap->trailing();
    const size_t len = cap->trailing_len();
    if (len == 1) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    if (data[0] == ';') {
        parser->command = Command();
        parser->command.key = Command::Key::clipboard_contents;
        parser->command.clipboard_contents.kind = 'c';
        parser->command.clipboard_contents.data = ZStr(data + 1, len - 1 - 1);
        parser->command.clipboard_contents.terminator =
            terminator_init(has_ch, terminator_ch);
    } else {
        if (len < 2) {
            parser->state = Parser::State::invalid;
            return nullptr;
        }
        if (data[1] != ';') {
            parser->state = Parser::State::invalid;
            return nullptr;
        }
        parser->command = Command();
        parser->command.key = Command::Key::clipboard_contents;
        parser->command.clipboard_contents.kind = (uint8_t)data[0];
        parser->command.clipboard_contents.data = ZStr(data + 2, len - 1 - 2);
        parser->command.clipboard_contents.terminator =
            terminator_init(has_ch, terminator_ch);
    }
    return &parser->command;
}
} /* namespace clipboard_operation */

namespace kitty_dnd_protocol {
inline Command *parse(Parser *parser, bool has_ch, uint8_t terminator_ch) {
    /* assert(parser.state == .@"72") */
    if (!parser->has_capture) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    Parser::Capture *cap = &parser->capture;

    const char *data = cap->trailing();
    const size_t data_len = cap->trailing_len();

    ZStr metadata(data, data_len);
    bool has_payload = false;
    ZStr payload;
    const char *sep = (const char *)memchr(data, ';', data_len);
    if (sep) {
        const size_t i = (size_t)(sep - data);
        metadata = ZStr(data, i);
        has_payload = true;
        payload = ZStr(data + i + 1, data_len - (i + 1));
    }

    parser->command = Command();
    parser->command.key = Command::Key::kitty_dnd_protocol;
    parser->command.kitty_dnd_protocol.metadata = metadata;
    parser->command.kitty_dnd_protocol.has_payload = has_payload;
    parser->command.kitty_dnd_protocol.payload = payload;
    parser->command.kitty_dnd_protocol.terminator = terminator_init(has_ch, terminator_ch);

    return &parser->command;
}
} /* namespace kitty_dnd_protocol */

namespace rxvt_extension {
/* Parse OSC 777 */
inline Command *parse(Parser *parser, bool, uint8_t) {
    if (!parser->has_capture) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    Parser::Capture *cap = &parser->capture;
    /* ensure that we are sentinel terminated */
    if (!cap->writer.writeByte(0)) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    char *data = cap->trailing();
    const size_t data_len = cap->trailing_len();
    const char *semi = (const char *)memchr(data, ';', data_len);
    if (!semi) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    const size_t k = (size_t)(semi - data);
    if (!(k == 6 && memcmp(data, "notify", 6) == 0)) {
        /* log.warn("unknown rxvt extension: {s}") */
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    const char *semi2 = (const char *)memchr(data + k + 1, ';', data_len - (k + 1));
    if (!semi2) {
        /* log.warn("rxvt notify extension is missing the title") */
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    const size_t t = (size_t)(semi2 - data);
    data[t] = 0;
    parser->command = Command();
    parser->command.key = Command::Key::show_desktop_notification;
    parser->command.show_desktop_notification.title = ZStr(data + k + 1, t - (k + 1));
    parser->command.show_desktop_notification.body = ZStr(data + t + 1, data_len - 1 - (t + 1));
    return &parser->command;
}
} /* namespace rxvt_extension */

namespace color {

using ::wisp::terminal::osc::color::Operation;
using ::wisp::terminal::osc::color::Target;
using ::wisp::terminal::osc::color::Request;
using ::wisp::terminal::osc::color::List;

/* Wisp: std.fmt.parseInt(u9, s, 10). An optional leading '+' is accepted;
 * a value above 511 overflows. Zig additionally accepts '-' on zero and
 * '_' separators, which appear in none of upstream's tests and are not
 * reproduced. */
inline bool parse_u9(const char *s, size_t len, uint16_t *out) {
    size_t i = 0;
    if (i < len && s[i] == '+') i++;
    if (i >= len) return false;
    unsigned v = 0;
    for (; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (unsigned)(s[i] - '0');
        if (v > 511) return false;
    }
    *out = (uint16_t)v;
    return true;
}

/* Wisp: std.mem.tokenizeScalar(u8, buf, ';') — empty tokens are skipped,
 * which is not what split does, and upstream relies on the difference. */
struct TokenIterator {
    const char *buf;
    size_t      len;
    size_t      index;

    bool next(const char **tok, size_t *tok_len) {
        while (index < len && buf[index] == ';') index++;
        if (index >= len) return false;
        const size_t start = index;
        while (index < len && buf[index] != ';') index++;
        *tok = buf + start;
        *tok_len = index - start;
        return true;
    }
};

inline bool is_query(const char *s, size_t len) { return len == 1 && s[0] == '?'; }

/* OSC 4/5 */
inline bool parseGetSetAnsiColor(Operation op, TokenIterator *it, List *result) {
    /* Note: in ANY error scenario below we return the accumulated results.
     * This matches the xterm behavior (see misc.c ChangeAnsiColorRequest) */

    while (true) {
        /* We expect a `c; spec` pair. If either doesn't exist then
         * we return the results up to this point. */
        const char *color_str, *spec_str;
        size_t color_len, spec_len;
        if (!it->next(&color_str, &color_len)) return true;
        if (!it->next(&spec_str, &spec_len)) return true;

        /* Color must be numeric. u9 because that'll fit our palette +
         * special */
        uint16_t color;
        if (!parse_u9(color_str, color_len, &color)) return true;

        /* Parse the color. */
        Target target;
        if (op == Operation::osc_5) {
            /* OSC5 maps directly to the Special enum. */
            if (color > 7 || color > 4) return true;
            target = Target::makeSpecial((::wisp::terminal::Special)color);
        } else {
            /* OSC4 maps 0-255 to palette, 256-259 to special offset
             * by the palette count. */
            if (color <= 255) {
                target = Target::makePalette((uint8_t)color);
            } else {
                const unsigned sp = (unsigned)color - 256;
                if (sp > 7 || sp > 4) return true;
                target = Target::makeSpecial((::wisp::terminal::Special)sp);
            }
        }

        /* "?" always results in a query. */
        if (is_query(spec_str, spec_len)) {
            Request *req = result->addOne();
            if (!req) return false;
            *req = Request();
            req->tag = Request::Tag::query;
            req->query = target;
            continue;
        }

        ::wisp::terminal::RGB rgb;
        if (::wisp::terminal::RGB::parse(spec_str, spec_len, &rgb) !=
            ::wisp::terminal::ColorError::none) {
            return true;
        }
        Request *req = result->addOne();
        if (!req) return false;
        *req = Request();
        req->tag = Request::Tag::set;
        req->set.target = target;
        req->set.color = rgb;
    }
}

/* OSC 104/105: Reset ANSI Colors */
inline bool parseResetAnsiColor(Operation op, TokenIterator *it, List *result) {
    /* Note: xterm stops parsing the reset list on any error, but we're
     * more flexible and try the next value. This matches the behavior of
     * Kitty and I don't see a downside to being more flexible here.
     * Hopefully no one depends on the exact behavior of xterm. */

    while (true) {
        const char *color_str;
        size_t color_len;
        if (!it->next(&color_str, &color_len)) {
            /* If no parameters are given, we reset the full table. */
            if (result->count() == 0) {
                Request *req = result->addOne();
                if (!req) return false;
                *req = Request();
                req->tag = op == Operation::osc_104 ? Request::Tag::reset_palette
                                                    : Request::Tag::reset_special;
            }
            return true;
        }

        /* Empty color strings are ignored, not treated as an error. */
        if (color_len == 0) continue;

        /* Color must be numeric. u9 because that'll fit our palette +
         * special */
        uint16_t color;
        if (!parse_u9(color_str, color_len, &color)) continue;

        /* Parse the color. */
        Target target;
        if (op == Operation::osc_105) {
            /* OSC105 maps directly to the Special enum. */
            if (color > 7 || color > 4) continue;
            target = Target::makeSpecial((::wisp::terminal::Special)color);
        } else {
            /* OSC104 maps 0-255 to palette, 256-259 to special offset
             * by the palette count. */
            if (color <= 255) {
                target = Target::makePalette((uint8_t)color);
            } else {
                const unsigned sp = (unsigned)color - 256;
                if (sp > 7 || sp > 4) continue;
                target = Target::makeSpecial((::wisp::terminal::Special)sp);
            }
        }

        Request *req = result->addOne();
        if (!req) return false;
        *req = Request();
        req->tag = Request::Tag::reset;
        req->reset = target;
    }
}

/* OSC 10-19: Get/Set Dynamic Colors */
inline bool parseGetSetDynamicColor(::wisp::terminal::Dynamic start,
                                    TokenIterator *it, List *result) {
    /* Note: in ANY error scenario below we return the accumulated results.
     * This matches the xterm behavior (see misc.c ChangeColorsRequest) */

    ::wisp::terminal::Dynamic color = start;
    while (true) {
        const char *spec_str;
        size_t spec_len;
        if (!it->next(&spec_str, &spec_len)) return true;

        if (is_query(spec_str, spec_len)) {
            Request *req = result->addOne();
            if (!req) return false;
            *req = Request();
            req->tag = Request::Tag::query;
            req->query = Target::makeDynamic(color);
        } else {
            ::wisp::terminal::RGB rgb;
            if (::wisp::terminal::RGB::parse(spec_str, spec_len, &rgb) !=
                ::wisp::terminal::ColorError::none) {
                return true;
            }
            Request *req = result->addOne();
            if (!req) return false;
            *req = Request();
            req->tag = Request::Tag::set;
            req->set.target = Target::makeDynamic(color);
            req->set.color = rgb;
        }

        /* Each successive value uses the next color so long as it exists. */
        if (!::wisp::terminal::dynamic_next(color, &color)) return true;
    }
}

/* OSC 110-119: Reset Dynamic Colors */
inline bool parseResetDynamicColor(::wisp::terminal::Dynamic color,
                                   TokenIterator *it, List *result) {
    const char *tok;
    size_t tok_len;
    if (it->next(&tok, &tok_len)) return true;
    Request *req = result->addOne();
    if (!req) return false;
    *req = Request();
    req->tag = Request::Tag::reset;
    req->reset = Target::makeDynamic(color);
    return true;
}

/* Parse any color operation string. This should NOT include the operation
 * itself, but only the body of the operation. e.g. for "4;a;b;c" the body
 * should be "a;b;c" and the operation should be set accordingly.
 *
 * Color parsing is fairly complicated so we pull this out to a specialized
 * function rather than go through our OSC parsing state machine. This is
 * much slower and requires more memory (since we need to buffer the full
 * request) but grants us an easier to understand and testable
 * implementation.
 *
 * If color changing ends up being a bottleneck we can optimize this later.
 *
 * Wisp: ParseError!List is the bool return plus *out; false means an
 * allocation failed. */
inline bool parseColor(Operation op, const char *buf, size_t len, List *out) {
    typedef ::wisp::terminal::Dynamic D;
    TokenIterator it;
    it.buf = buf;
    it.len = len;
    it.index = 0;
    switch (op) {
        case Operation::osc_4: return parseGetSetAnsiColor(Operation::osc_4, &it, out);
        case Operation::osc_5: return parseGetSetAnsiColor(Operation::osc_5, &it, out);
        case Operation::osc_104: return parseResetAnsiColor(Operation::osc_104, &it, out);
        case Operation::osc_105: return parseResetAnsiColor(Operation::osc_105, &it, out);
        case Operation::osc_10: return parseGetSetDynamicColor(D::foreground, &it, out);
        case Operation::osc_11: return parseGetSetDynamicColor(D::background, &it, out);
        case Operation::osc_12: return parseGetSetDynamicColor(D::cursor, &it, out);
        case Operation::osc_13: return parseGetSetDynamicColor(D::pointer_foreground, &it, out);
        case Operation::osc_14: return parseGetSetDynamicColor(D::pointer_background, &it, out);
        case Operation::osc_15: return parseGetSetDynamicColor(D::tektronix_foreground, &it, out);
        case Operation::osc_16: return parseGetSetDynamicColor(D::tektronix_background, &it, out);
        case Operation::osc_17: return parseGetSetDynamicColor(D::highlight_background, &it, out);
        case Operation::osc_18: return parseGetSetDynamicColor(D::tektronix_cursor, &it, out);
        case Operation::osc_19: return parseGetSetDynamicColor(D::highlight_foreground, &it, out);
        case Operation::osc_110: return parseResetDynamicColor(D::foreground, &it, out);
        case Operation::osc_111: return parseResetDynamicColor(D::background, &it, out);
        case Operation::osc_112: return parseResetDynamicColor(D::cursor, &it, out);
        case Operation::osc_113: return parseResetDynamicColor(D::pointer_foreground, &it, out);
        case Operation::osc_114: return parseResetDynamicColor(D::pointer_background, &it, out);
        case Operation::osc_115: return parseResetDynamicColor(D::tektronix_foreground, &it, out);
        case Operation::osc_116: return parseResetDynamicColor(D::tektronix_background, &it, out);
        case Operation::osc_117: return parseResetDynamicColor(D::highlight_background, &it, out);
        case Operation::osc_118: return parseResetDynamicColor(D::tektronix_cursor, &it, out);
        case Operation::osc_119: return parseResetDynamicColor(D::highlight_foreground, &it, out);
    }
    return true;
}

/* Parse OSCs 4, 5, 10-19, 104, 110-119 */
inline Command *parse(Parser *parser, bool has_ch, uint8_t terminator_ch) {
    if (!parser->alloc) {
        parser->state = Parser::State::invalid;
        return nullptr;
    }
    /* If we've collected any extra data parse that, otherwise use an empty
     * string. */
    const char *data = "";
    size_t data_len = 0;
    if (parser->has_capture) {
        data = parser->capture.trailing();
        data_len = parser->capture.trailing_len();
    }

    /* Check and make sure that we're parsing the correct OSCs */
    typedef Parser::State S;
    Operation op;
    switch (parser->state) {
        case S::s4: op = Operation::osc_4; break;
        case S::s5: op = Operation::osc_5; break;
        case S::s10: op = Operation::osc_10; break;
        case S::s11: op = Operation::osc_11; break;
        case S::s12: op = Operation::osc_12; break;
        case S::s13: op = Operation::osc_13; break;
        case S::s14: op = Operation::osc_14; break;
        case S::s15: op = Operation::osc_15; break;
        case S::s16: op = Operation::osc_16; break;
        case S::s17: op = Operation::osc_17; break;
        case S::s18: op = Operation::osc_18; break;
        case S::s19: op = Operation::osc_19; break;
        case S::s104: op = Operation::osc_104; break;
        case S::s110: op = Operation::osc_110; break;
        case S::s111: op = Operation::osc_111; break;
        case S::s112: op = Operation::osc_112; break;
        case S::s113: op = Operation::osc_113; break;
        case S::s114: op = Operation::osc_114; break;
        case S::s115: op = Operation::osc_115; break;
        case S::s116: op = Operation::osc_116; break;
        case S::s117: op = Operation::osc_117; break;
        case S::s118: op = Operation::osc_118; break;
        case S::s119: op = Operation::osc_119; break;
        default:
            parser->state = S::invalid;
            return nullptr;
    }

    parser->command = Command();
    parser->command.key = Command::Key::color_operation;
    parser->command.color_operation.op = op;
    if (!parseColor(op, data, data_len, &parser->command.color_operation.requests)) {
        /* log.info("failed to parse OSC ... color request") and an empty
         * list, as upstream falls back to .{} */
        parser->command.color_operation.requests.deinit();
    }
    parser->command.color_operation.terminator = terminator_init(has_ch, terminator_ch);
    return &parser->command;
}

} /* namespace color */

} /* namespace parsers */

inline Command *Parser::end(bool has_ch, uint8_t ch) {
    typedef State S;

    switch (state) {
        case S::start: return nullptr;
        case S::invalid: return nullptr;

        case S::s0:
        case S::s2:
            return parsers::change_window_title::parse(this, has_ch, ch);

        case S::s1:
            return parsers::change_window_icon::parse(this, has_ch, ch);

        case S::s4: case S::s5:
        case S::s10: case S::s11: case S::s12: case S::s13: case S::s14:
        case S::s15: case S::s16: case S::s17: case S::s18: case S::s19:
        case S::s104:
        case S::s110: case S::s111: case S::s112: case S::s113: case S::s114:
        case S::s115: case S::s116: case S::s117: case S::s118: case S::s119:
            return parsers::color::parse(this, has_ch, ch);

        case S::s7:
            return parsers::report_pwd::parse(this, has_ch, ch);

        case S::s8:
            return parsers::hyperlink::parse(this, has_ch, ch);

        /* Wisp: parsers.osc9 — not yet transliterated. */
        case S::s9: return nullptr;

        /* Wisp: parsers.kitty_color — not yet transliterated. */
        case S::s21: return nullptr;

        case S::s22:
            return parsers::mouse_shape::parse(this, has_ch, ch);

        case S::s52:
            return parsers::clipboard_operation::parse(this, has_ch, ch);

        case S::s55: return nullptr;

        case S::s3:
        case S::s30:
        case S::s300:
            return nullptr;

        /* Wisp: parsers.context_signal — not yet transliterated. */
        case S::s3008: return nullptr;

        case S::s6: return nullptr;

        /* Wisp: parsers.kitty_text_sizing — not yet transliterated. */
        case S::s66: return nullptr;

        case S::s72:
            return parsers::kitty_dnd_protocol::parse(this, has_ch, ch);

        case S::s77: return nullptr;

        /* Wisp: parsers.kitty_desktop_notification — not yet
         * transliterated. */
        case S::s99: return nullptr;

        /* Wisp: parsers.semantic_prompt — not yet transliterated. */
        case S::s133: return nullptr;

        case S::s552: return nullptr;

        case S::s777: return parsers::rxvt_extension::parse(this, has_ch, ch);

        /* Wisp: parsers.iterm2 — not yet transliterated. */
        case S::s1337: return nullptr;

        /* Wisp: parsers.kitty_clipboard_protocol — not yet transliterated. */
        case S::s5522: return nullptr;
    }
    return nullptr;
}

} /* namespace osc */
} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_OSC_HPP */
