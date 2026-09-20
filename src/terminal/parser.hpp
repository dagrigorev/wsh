/* Ported from Ghostty src/terminal/Parser.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * The VT state machine: bytes in, actions out.
 *
 * This is Paul Williams' DEC parser, the same state diagram every terminal
 * has been built from since it was published. It is worth saying why a state
 * machine rather than the obvious reading loop: escape sequences can be cut
 * anywhere. A program writing to a pipe gets its output split wherever the
 * kernel felt like splitting it, and half of a CSI can arrive now and the
 * rest after the next read. A parser that keeps all its state in explicit
 * fields can be fed one byte at a time and does not care.
 *
 * It also has to cope with sequences that are simply wrong, since a terminal
 * cannot stop and complain. The rule throughout is that a malformed sequence
 * is abandoned rather than guessed at, and the bytes that follow are treated
 * as ordinary text — which is what the diagram's "ignore" states are for.
 *
 * BYTES, NOT CHARACTERS. This parser works in bytes. UTF-8 decoding happens
 * above it, because the state machine only cares about the ASCII range and
 * feeding it decoded codepoints would mean the escape sequence grammar had to
 * know about Unicode. Upstream splits them the same way.
 *
 * NOT THE LIVE PARSER. Wisp's working parser is the C code under
 * src/terminal. This is the ported one, and nothing runs on it yet.
 */

#pragma once
#ifndef WISP_TERMINAL_PARSER_HPP
#define WISP_TERMINAL_PARSER_HPP

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wisp {
namespace terminal {
namespace parser {

/* ─── limits ─────────────────────────────────────────────────────────────── */

/* A sequence with more parameters than this is malformed by any reading, and
 * the diagram says to keep parsing but stop recording. Sixteen is what xterm
 * allows and therefore what programs assume. */
static const size_t MAX_PARAMS = 16;

/* CSI intermediates are at most two in practice; the grammar allows more but
 * nothing uses them. */
static const size_t MAX_INTERMEDIATES = 2;

/* OSC strings carry things like window titles and hyperlink URIs. A longer
 * one is truncated rather than dropped, since the useful part is usually at
 * the front. */
static const size_t MAX_STRING = 1024;

/* ─── actions ────────────────────────────────────────────────────────────── */

enum class ActionTag : uint8_t {
    none = 0,

    /* An ordinary byte to put on the screen. */
    print,

    /* A C0 control: line feed, carriage return, tab and the rest. */
    execute,

    /* A complete CSI, the sequences that start ESC [. */
    csi_dispatch,

    /* A complete two-byte escape sequence, like ESC M. */
    esc_dispatch,

    /* A complete OSC — the string is in the parser's buffer. */
    osc_dispatch,

    /* A device control string: hook opens it, put feeds it, unhook ends it.
     * Nothing here interprets them; they are reported so that a caller which
     * cares can, and so that one which does not can still skip them
     * correctly. */
    dcs_hook,
    dcs_put,
    dcs_unhook,
};

/* One thing that happened. */
struct Action {
    ActionTag tag;

    /* print and execute and dcs_put: the byte. */
    uint8_t byte;

    /* csi_dispatch and esc_dispatch: the byte that ended the sequence. */
    uint8_t final_byte;

    /* csi_dispatch: the private marker, if the sequence had one — the '?' of
     * ESC [ ? 25 h. Zero when there was none. */
    uint8_t private_marker;

    /* The intermediates collected before the final byte. */
    uint8_t intermediates[MAX_INTERMEDIATES];
    uint8_t intermediate_count;

    /* The numeric parameters. */
    uint16_t params[MAX_PARAMS];
    uint8_t  param_count;

    /* Which parameters were joined to the one before them with a colon
     * rather than a semicolon.
     *
     * SGR's extended colours use both: 38;2;R;G;B and 38:2::R:G:B mean the
     * same thing, and 4:3 means a curly underline where 4;3 means underline
     * then italic. Losing the difference would make those two
     * indistinguishable, so the separator is recorded per parameter. */
    uint16_t param_is_sub;

    /* osc_dispatch: the string, and whether it was cut short. */
    const char *string;
    size_t      string_len;
    bool        string_truncated;

    Action()
        : tag(ActionTag::none), byte(0), final_byte(0), private_marker(0),
          intermediate_count(0), param_count(0), param_is_sub(0),
          string(nullptr), string_len(0), string_truncated(false) {
        memset(intermediates, 0, sizeof(intermediates));
        memset(params, 0, sizeof(params));
    }
};

/* A byte can produce more than one action — a C0 control inside a CSI both
 * executes and leaves the sequence unfinished, and the byte that ends one
 * sequence can begin the next. Three is the most the diagram can produce. */
struct Actions {
    Action list[3];
    uint8_t count;

    Actions() : count(0) {}

    void push(const Action &a) {
        if (count < 3) list[count++] = a;
    }
};

/* ─── the states ─────────────────────────────────────────────────────────── */

enum class State : uint8_t {
    ground = 0,
    escape,
    escape_intermediate,
    csi_entry,
    csi_param,
    csi_intermediate,
    csi_ignore,
    dcs_entry,
    dcs_param,
    dcs_intermediate,
    dcs_passthrough,
    dcs_ignore,
    osc_string,

    /* SOS, PM and APC all mean "a string nothing here understands". They are
     * consumed to their terminator and discarded, which is the correct
     * handling: the alternative is printing somebody else's protocol onto the
     * screen. */
    sos_pm_apc_string,
};

struct Parser {
    State state;

    uint8_t intermediates[MAX_INTERMEDIATES];
    uint8_t intermediate_count;
    bool    intermediates_overflowed;

    uint16_t params[MAX_PARAMS];
    uint8_t  param_count;
    uint16_t param_is_sub;
    bool     param_pending;   /* digits have been seen since the last separator */

    /* More parameters arrived than can be recorded. The sequence is still
     * dispatched with the ones that fit — a program sending eighteen
     * parameters meant the first sixteen as much as it meant the rest — but
     * the extra digits must not run into the last one it kept. */
    bool param_overflowed;

    uint8_t private_marker;

    char   string[MAX_STRING];
    size_t string_len;
    bool   string_truncated;

    Parser() { memset(this, 0, sizeof(*this)); }
};

/* ─── byte classes ───────────────────────────────────────────────────────── */

inline bool is_c0(uint8_t b) {
    return b <= 0x17 || b == 0x19 || (b >= 0x1C && b <= 0x1F);
}
inline bool is_intermediate(uint8_t b) { return b >= 0x20 && b <= 0x2F; }
inline bool is_param_byte(uint8_t b) { return b >= 0x30 && b <= 0x3F; }
inline bool is_final(uint8_t b) { return b >= 0x40 && b <= 0x7E; }
inline bool is_digit(uint8_t b) { return b >= 0x30 && b <= 0x39; }

/* ─── the machine ────────────────────────────────────────────────────────── */

/* Forget everything collected for a sequence. The diagram calls this "clear"
 * and runs it when a sequence begins, which is what makes a parser recover
 * from a malformed one: nothing from the last sequence can leak into the
 * next. */
inline void parser_clear(Parser *p) {
    p->intermediate_count = 0;
    p->intermediates_overflowed = false;
    memset(p->intermediates, 0, sizeof(p->intermediates));

    p->param_count = 0;
    p->param_is_sub = 0;
    p->param_pending = false;
    p->param_overflowed = false;
    memset(p->params, 0, sizeof(p->params));

    p->private_marker = 0;

    p->string_len = 0;
    p->string_truncated = false;
}

inline void parser_collect(Parser *p, uint8_t b) {
    if (p->intermediate_count < MAX_INTERMEDIATES) {
        p->intermediates[p->intermediate_count++] = b;
    } else {
        /* Too many. The sequence is kept but will be ignored at the end,
         * which is the diagram's rule — a sequence nobody can have meant
         * should not be acted on halfway. */
        p->intermediates_overflowed = true;
    }
}

/* Start a new parameter, remembering whether a colon joined it to the last. */
inline void parser_param_next(Parser *p, bool sub) {
    if (p->param_count < MAX_PARAMS) {
        if (sub && p->param_count > 0) {
            p->param_is_sub |= (uint16_t)(1u << p->param_count);
        }
        p->param_count++;
    } else {
        p->param_overflowed = true;
    }
    p->param_pending = false;
}

inline void parser_param_digit(Parser *p, uint8_t b) {
    if (p->param_overflowed) return;

    if (p->param_count == 0) p->param_count = 1;
    p->param_pending = true;

    uint32_t v = p->params[p->param_count - 1];
    v = v * 10 + (uint32_t)(b - '0');
    /* Saturate rather than wrap. A parameter this large is meaningless
     * anyway, and wrapping would turn it into a small number that looks
     * deliberate. */
    p->params[p->param_count - 1] = v > 65535 ? (uint16_t)65535 : (uint16_t)v;
}

/* Fill in the parts of an action that come from what has been collected. */
inline void parser_fill(const Parser *p, Action *a) {
    a->private_marker = p->private_marker;

    a->intermediate_count = p->intermediate_count;
    for (size_t i = 0; i < MAX_INTERMEDIATES; i++) {
        a->intermediates[i] = p->intermediates[i];
    }

    a->param_count = p->param_count > MAX_PARAMS ? (uint8_t)MAX_PARAMS
                                                 : p->param_count;
    for (size_t i = 0; i < MAX_PARAMS; i++) a->params[i] = p->params[i];
    a->param_is_sub = p->param_is_sub;
}

inline Action parser_action(ActionTag tag) {
    Action a;
    a.tag = tag;
    return a;
}

/* Append to the string a control or OSC sequence is carrying. */
inline void parser_string_put(Parser *p, uint8_t b) {
    if (p->string_len + 1 < MAX_STRING) {
        p->string[p->string_len++] = (char)b;
    } else {
        p->string_truncated = true;
    }
}

/* End an OSC, if one is open. */
inline void parser_osc_end(Parser *p, Actions *out) {
    Action a = parser_action(ActionTag::osc_dispatch);
    p->string[p->string_len] = '\0';
    a.string = p->string;
    a.string_len = p->string_len;
    a.string_truncated = p->string_truncated;
    out->push(a);
}

inline void parser_dcs_unhook(Parser *p, Actions *out) {
    (void)p;
    out->push(parser_action(ActionTag::dcs_unhook));
}

/* Feed one byte, and get back what it meant. */
inline Actions parser_next(Parser *p, uint8_t b) {
    Actions out;

    /* Three bytes mean the same thing in nearly every state, so they are
     * handled once here rather than in each. CAN and SUB abandon whatever is
     * in progress; ESC begins a new sequence even in the middle of one, which
     * is what lets a parser recover from a sequence that was never finished.
     */
    if (b == 0x18 || b == 0x1A) {
        if (p->state == State::dcs_passthrough) parser_dcs_unhook(p, &out);

        Action a = parser_action(ActionTag::execute);
        a.byte = b;
        out.push(a);

        p->state = State::ground;
        return out;
    }

    if (b == 0x1B) {
        if (p->state == State::dcs_passthrough) parser_dcs_unhook(p, &out);

        /* An OSC broken off by an ESC is still reported: the ESC may be the
         * start of the ST that ends it, and if it is not, the string was
         * complete as far as anyone can tell. */
        if (p->state == State::osc_string) parser_osc_end(p, &out);

        parser_clear(p);
        p->state = State::escape;
        return out;
    }

    switch (p->state) {
        case State::ground: {
            if (is_c0(b)) {
                Action a = parser_action(ActionTag::execute);
                a.byte = b;
                out.push(a);
                return out;
            }
            if (b == 0x7F) return out;   /* DEL is not printable */

            Action a = parser_action(ActionTag::print);
            a.byte = b;
            out.push(a);
            return out;
        }

        case State::escape: {
            if (is_c0(b)) {
                Action a = parser_action(ActionTag::execute);
                a.byte = b;
                out.push(a);
                return out;
            }
            if (b == 0x7F) return out;

            if (is_intermediate(b)) {
                parser_collect(p, b);
                p->state = State::escape_intermediate;
                return out;
            }

            /* The three that open a string rather than ending a sequence. */
            if (b == 'P') {
                p->state = State::dcs_entry;
                return out;
            }
            if (b == ']') {
                p->state = State::osc_string;
                return out;
            }
            if (b == 'X' || b == '^' || b == '_') {
                p->state = State::sos_pm_apc_string;
                return out;
            }
            if (b == '[') {
                p->state = State::csi_entry;
                return out;
            }

            Action a = parser_action(ActionTag::esc_dispatch);
            a.final_byte = b;
            parser_fill(p, &a);
            out.push(a);
            p->state = State::ground;
            return out;
        }

        case State::escape_intermediate: {
            if (is_c0(b)) {
                Action a = parser_action(ActionTag::execute);
                a.byte = b;
                out.push(a);
                return out;
            }
            if (b == 0x7F) return out;

            if (is_intermediate(b)) {
                parser_collect(p, b);
                return out;
            }

            Action a = parser_action(ActionTag::esc_dispatch);
            a.final_byte = b;
            parser_fill(p, &a);
            out.push(a);
            p->state = State::ground;
            return out;
        }

        case State::csi_entry:
        case State::csi_param: {
            if (is_c0(b)) {
                Action a = parser_action(ActionTag::execute);
                a.byte = b;
                out.push(a);
                return out;
            }
            if (b == 0x7F) return out;

            if (is_digit(b)) {
                parser_param_digit(p, b);
                p->state = State::csi_param;
                return out;
            }
            if (b == ';' || b == ':') {
                parser_param_next(p, b == ':');
                p->state = State::csi_param;
                return out;
            }
            if (b >= 0x3C && b <= 0x3F) {
                /* A private marker, and only valid before anything else. */
                if (p->state == State::csi_entry && p->param_count == 0) {
                    p->private_marker = b;
                    return out;
                }
                p->state = State::csi_ignore;
                return out;
            }
            if (is_param_byte(b)) {
                p->state = State::csi_ignore;
                return out;
            }
            if (is_intermediate(b)) {
                parser_collect(p, b);
                p->state = State::csi_intermediate;
                return out;
            }

            if (is_final(b)) {
                if (p->param_pending || p->param_count > 0) {
                    /* A trailing separator leaves an empty parameter, which
                     * is a real value — CSI 1;H means the same as CSI 1;1H.
                     */
                }
                if (!p->intermediates_overflowed) {
                    Action a = parser_action(ActionTag::csi_dispatch);
                    a.final_byte = b;
                    parser_fill(p, &a);
                    out.push(a);
                }
                p->state = State::ground;
                return out;
            }

            p->state = State::ground;
            return out;
        }

        case State::csi_intermediate: {
            if (is_c0(b)) {
                Action a = parser_action(ActionTag::execute);
                a.byte = b;
                out.push(a);
                return out;
            }
            if (b == 0x7F) return out;

            if (is_intermediate(b)) {
                parser_collect(p, b);
                return out;
            }
            if (is_param_byte(b)) {
                /* A parameter after an intermediate is out of order, and the
                 * diagram says the whole sequence is void. */
                p->state = State::csi_ignore;
                return out;
            }
            if (is_final(b)) {
                if (!p->intermediates_overflowed) {
                    Action a = parser_action(ActionTag::csi_dispatch);
                    a.final_byte = b;
                    parser_fill(p, &a);
                    out.push(a);
                }
                p->state = State::ground;
                return out;
            }

            p->state = State::ground;
            return out;
        }

        case State::csi_ignore: {
            if (is_c0(b)) {
                Action a = parser_action(ActionTag::execute);
                a.byte = b;
                out.push(a);
                return out;
            }
            /* Everything up to the final byte is swallowed, and the final
             * byte ends the sequence without dispatching it. */
            if (is_final(b)) p->state = State::ground;
            return out;
        }

        case State::dcs_entry:
        case State::dcs_param: {
            if (is_c0(b) || b == 0x7F) return out;

            if (is_digit(b)) {
                parser_param_digit(p, b);
                p->state = State::dcs_param;
                return out;
            }
            if (b == ';' || b == ':') {
                parser_param_next(p, b == ':');
                p->state = State::dcs_param;
                return out;
            }
            if (b >= 0x3C && b <= 0x3F) {
                if (p->state == State::dcs_entry && p->param_count == 0) {
                    p->private_marker = b;
                    return out;
                }
                p->state = State::dcs_ignore;
                return out;
            }
            if (is_param_byte(b)) {
                p->state = State::dcs_ignore;
                return out;
            }
            if (is_intermediate(b)) {
                parser_collect(p, b);
                p->state = State::dcs_intermediate;
                return out;
            }
            if (is_final(b)) {
                Action a = parser_action(ActionTag::dcs_hook);
                a.final_byte = b;
                parser_fill(p, &a);
                out.push(a);
                p->state = State::dcs_passthrough;
                return out;
            }

            p->state = State::dcs_ignore;
            return out;
        }

        case State::dcs_intermediate: {
            if (is_c0(b) || b == 0x7F) return out;

            if (is_intermediate(b)) {
                parser_collect(p, b);
                return out;
            }
            if (is_param_byte(b)) {
                p->state = State::dcs_ignore;
                return out;
            }
            if (is_final(b)) {
                Action a = parser_action(ActionTag::dcs_hook);
                a.final_byte = b;
                parser_fill(p, &a);
                out.push(a);
                p->state = State::dcs_passthrough;
                return out;
            }

            p->state = State::dcs_ignore;
            return out;
        }

        case State::dcs_passthrough: {
            if (b == 0x9C) {
                parser_dcs_unhook(p, &out);
                p->state = State::ground;
                return out;
            }
            if (b == 0x7F) return out;

            Action a = parser_action(ActionTag::dcs_put);
            a.byte = b;
            out.push(a);
            return out;
        }

        case State::dcs_ignore: {
            if (b == 0x9C) p->state = State::ground;
            return out;
        }

        case State::osc_string: {
            /* BEL ends an OSC. It is not in the original diagram — xterm
             * allowed it and every program uses it, so a parser that insisted
             * on ST would fail on most of the titles it is ever sent. */
            if (b == 0x07) {
                parser_osc_end(p, &out);
                p->state = State::ground;
                return out;
            }
            if (b == 0x9C) {
                parser_osc_end(p, &out);
                p->state = State::ground;
                return out;
            }

            parser_string_put(p, b);
            return out;
        }

        case State::sos_pm_apc_string: {
            if (b == 0x9C) p->state = State::ground;
            return out;
        }
    }

    return out;
}

/* ─── after an escape ────────────────────────────────────────────────────── */

/* The second half of ST — ESC \ — arrives as an esc_dispatch with a final
 * byte of backslash, because that is what it is. A caller wanting to know
 * whether a string ended can ask this rather than testing the byte. */
inline bool action_is_string_terminator(const Action &a) {
    return a.tag == ActionTag::esc_dispatch && a.final_byte == '\\';
}

/* The value of a parameter, or a default when it was omitted.
 *
 * Every CSI has defaults, and an omitted parameter is not zero — CSI H and
 * CSI 1;1H are the same thing, and CSI 0;0H is too, because zero means
 * "default" in most of them. Asking through here keeps that in one place
 * rather than at every use. */
inline uint16_t action_param(const Action &a, size_t index, uint16_t fallback) {
    if (index >= a.param_count) return fallback;
    const uint16_t v = a.params[index];
    return v == 0 ? fallback : v;
}

/* The raw value, for the parameters where zero means zero. */
inline uint16_t action_param_raw(const Action &a, size_t index,
                                 uint16_t fallback) {
    if (index >= a.param_count) return fallback;
    return a.params[index];
}

/* Whether a parameter was joined to the one before it with a colon. */
inline bool action_param_is_sub(const Action &a, size_t index) {
    if (index >= a.param_count || index >= MAX_PARAMS) return false;
    return (a.param_is_sub & (uint16_t)(1u << index)) != 0;
}

} /* namespace parser */
} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_PARSER_HPP */
