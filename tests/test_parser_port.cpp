/* Tests for src/terminal/parser.hpp.
 *
 * Related to Ghostty src/terminal/Parser.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Two things are being checked throughout. That a correct sequence produces
 * the right action, which is the easy half. And that a sequence split at an
 * arbitrary byte, or malformed in one of the ways real programs manage, does
 * something sensible — because a terminal cannot stop and complain.
 *
 * Named test_parser_port to keep it apart from test_vt_parser.c, which covers
 * the live parser this does not yet replace.
 */

#include "test_helpers.h"
#include "parser.hpp"

using namespace wisp::terminal::parser;

/* Feed a string, collecting every action it produced. */
struct Collected {
    Action list[64];
    size_t count;

    Collected() : count(0) {}

    void feed(Parser *p, const char *text) {
        for (const char *c = text; *c; c++) {
            Actions a = parser_next(p, (uint8_t)*c);
            for (uint8_t i = 0; i < a.count && count < 64; i++) {
                list[count++] = a.list[i];
            }
        }
    }
};

static Collected run(const char *text) {
    Parser p;
    Collected c;
    c.feed(&p, text);
    return c;
}

/* The text that was printed, ignoring everything else. */
static void printed(const Collected &c, char *out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < c.count && n + 1 < cap; i++) {
        if (c.list[i].tag == ActionTag::print) out[n++] = (char)c.list[i].byte;
    }
    out[n] = '\0';
}

static const Action *first(const Collected &c, ActionTag tag) {
    for (size_t i = 0; i < c.count; i++) {
        if (c.list[i].tag == tag) return &c.list[i];
    }
    return nullptr;
}

static size_t count_of(const Collected &c, ActionTag tag) {
    size_t n = 0;
    for (size_t i = 0; i < c.count; i++) {
        if (c.list[i].tag == tag) n++;
    }
    return n;
}

/* ─── ordinary text ──────────────────────────────────────────────────────── */

TEST(parser, prints_plain_text) {
    Collected c = run("hello");

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "hello") == 0);
    ASSERT_EQ(c.count, 5u);
}

TEST(parser, control_characters_execute) {
    Collected c = run("a\r\nb");

    ASSERT_EQ(count_of(c, ActionTag::execute), 2u);
    ASSERT_EQ(c.list[1].byte, '\r');
    ASSERT_EQ(c.list[2].byte, '\n');

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "ab") == 0);
}

TEST(parser, del_is_not_printable) {
    Collected c = run("a\x7f" "b");

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "ab") == 0);
    ASSERT_EQ(c.count, 2u);
}

TEST(parser, high_bytes_print) {
    /* UTF-8 is decoded above this, so a continuation byte is just a byte
     * here. The state machine only cares about the ASCII range. */
    Collected c = run("\xc3\xa9");
    ASSERT_EQ(count_of(c, ActionTag::print), 2u);
}

/* ─── escape sequences ───────────────────────────────────────────────────── */

TEST(parser, a_two_byte_escape) {
    Collected c = run("\x1b" "M");

    const Action *a = first(c, ActionTag::esc_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->final_byte, 'M');
    ASSERT_EQ(a->intermediate_count, 0);
}

TEST(parser, an_escape_with_an_intermediate) {
    Collected c = run("\x1b" "(B");

    const Action *a = first(c, ActionTag::esc_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->final_byte, 'B');
    ASSERT_EQ(a->intermediate_count, 1);
    ASSERT_EQ(a->intermediates[0], '(');
}

TEST(parser, an_escape_returns_to_ground) {
    Collected c = run("\x1b" "Mtext");

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "text") == 0);
}

/* ─── CSI ────────────────────────────────────────────────────────────────── */

TEST(csi, with_no_parameters) {
    Collected c = run("\x1b[H");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->final_byte, 'H');
    ASSERT_EQ(a->param_count, 0);

    /* An omitted parameter is not zero: CSI H and CSI 1;1H are the same
     * thing, and asking through here is what keeps that in one place. */
    ASSERT_EQ(action_param(*a, 0, 1), 1);
}

TEST(csi, with_one_parameter) {
    Collected c = run("\x1b[5A");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->final_byte, 'A');
    ASSERT_EQ(a->param_count, 1);
    ASSERT_EQ(a->params[0], 5);
}

TEST(csi, with_several_parameters) {
    Collected c = run("\x1b[10;20;30H");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->param_count, 3);
    ASSERT_EQ(a->params[0], 10);
    ASSERT_EQ(a->params[1], 20);
    ASSERT_EQ(a->params[2], 30);
}

TEST(csi, an_omitted_parameter_is_still_a_parameter) {
    Collected c = run("\x1b[1;;3H");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->param_count, 3);
    ASSERT_EQ(a->params[0], 1);
    ASSERT_EQ(a->params[1], 0);
    ASSERT_EQ(a->params[2], 3);
    ASSERT_EQ(action_param(*a, 1, 7), 7);
}

TEST(csi, a_private_marker) {
    Collected c = run("\x1b[?25h");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->private_marker, '?');
    ASSERT_EQ(a->params[0], 25);
    ASSERT_EQ(a->final_byte, 'h');
}

TEST(csi, an_intermediate) {
    Collected c = run("\x1b[0 q");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->final_byte, 'q');
    ASSERT_EQ(a->intermediate_count, 1);
    ASSERT_EQ(a->intermediates[0], ' ');
    ASSERT_EQ(a->params[0], 0);
}

TEST(csi, colons_are_not_semicolons) {
    Collected c = run("\x1b[4:3m");

    /* 4:3 is a curly underline; 4;3 is underline then italic. Losing the
     * separator would make the two indistinguishable. */
    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->param_count, 2);
    ASSERT_EQ(a->params[0], 4);
    ASSERT_EQ(a->params[1], 3);
    ASSERT_FALSE(action_param_is_sub(*a, 0));
    ASSERT_TRUE(action_param_is_sub(*a, 1));
}

TEST(csi, a_mix_of_separators) {
    Collected c = run("\x1b[38:2::10:20:30m");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->param_count, 6);
    ASSERT_EQ(a->params[0], 38);
    ASSERT_EQ(a->params[1], 2);
    ASSERT_EQ(a->params[2], 0);
    ASSERT_EQ(a->params[3], 10);
    ASSERT_EQ(a->params[5], 30);
    ASSERT_TRUE(action_param_is_sub(*a, 1));
    ASSERT_TRUE(action_param_is_sub(*a, 5));
}

TEST(csi, a_huge_parameter_saturates) {
    Collected c = run("\x1b[99999999999m");

    /* Wrapping would turn a meaningless number into a small one that looks
     * deliberate. */
    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->params[0], 65535);
}

TEST(csi, too_many_parameters_are_dropped_not_the_sequence) {
    Collected c = run("\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18m");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->param_count, 16);
    ASSERT_EQ(a->params[0], 1);
    ASSERT_EQ(a->params[15], 16);
}

/* ─── malformed input ────────────────────────────────────────────────────── */

TEST(csi, an_out_of_order_parameter_voids_the_sequence) {
    Collected c = run("\x1b[1 !2m" "ok");

    /* A parameter after an intermediate is out of order, so the sequence is
     * abandoned rather than guessed at — and what follows is ordinary text. */
    ASSERT_EQ(count_of(c, ActionTag::csi_dispatch), 0u);

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "ok") == 0);
}

TEST(csi, a_stray_escape_abandons_what_was_in_progress) {
    Collected c = run("\x1b[12;\x1b[H");

    /* Half a sequence, then a new one. The second is dispatched and nothing
     * of the first leaks into it — which is what makes a parser recover from
     * output that was cut off. */
    ASSERT_EQ(count_of(c, ActionTag::csi_dispatch), 1u);
    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_EQ(a->final_byte, 'H');
    ASSERT_EQ(a->param_count, 0);
}

TEST(csi, cancel_abandons_a_sequence) {
    Collected c = run("\x1b[12\x18" "done");

    ASSERT_EQ(count_of(c, ActionTag::csi_dispatch), 0u);
    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "done") == 0);
}

TEST(csi, a_control_inside_a_sequence_still_executes) {
    Collected c = run("\x1b[1\r2m");

    /* The carriage return happens where it appears, and the sequence carries
     * on around it. This is what real terminals do and programs occasionally
     * rely on. */
    ASSERT_EQ(count_of(c, ActionTag::execute), 1u);
    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->params[0], 12);
}

TEST(csi, an_ignored_sequence_ends_at_its_final_byte) {
    Collected c = run("\x1b[1 !2m" "\x1b[5A");

    ASSERT_EQ(count_of(c, ActionTag::csi_dispatch), 1u);
    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_EQ(a->final_byte, 'A');
    ASSERT_EQ(a->params[0], 5);
}

/* ─── split input ────────────────────────────────────────────────────────── */

TEST(parser, a_sequence_split_anywhere_still_works) {
    /* Output arrives cut wherever the kernel felt like cutting it, so every
     * split has to give the same answer. */
    const char *seq = "\x1b[38;5;196m";

    for (size_t at = 1; at < strlen(seq); at++) {
        Parser p;
        Collected c;

        char head[32];
        memcpy(head, seq, at);
        head[at] = '\0';
        c.feed(&p, head);
        c.feed(&p, seq + at);

        const Action *a = first(c, ActionTag::csi_dispatch);
        ASSERT_TRUE(a != nullptr);
        ASSERT_EQ(a->final_byte, 'm');
        ASSERT_EQ(a->param_count, 3);
        ASSERT_EQ(a->params[2], 196);
    }
}

TEST(parser, state_survives_between_feeds) {
    Parser p;
    Collected c;

    c.feed(&p, "\x1b[");
    ASSERT_EQ(c.count, 0u);
    c.feed(&p, "7");
    ASSERT_EQ(c.count, 0u);
    c.feed(&p, "m");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->params[0], 7);
}

/* ─── OSC ────────────────────────────────────────────────────────────────── */

TEST(osc, ended_by_bel) {
    Collected c = run("\x1b]0;a title\x07");

    /* BEL is not in the original diagram. xterm allowed it and every program
     * uses it, so a parser that insisted on ST would fail on most of the
     * titles it is ever sent. */
    const Action *a = first(c, ActionTag::osc_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->string_len, 9u);
    ASSERT_TRUE(strcmp(a->string, "0;a title") == 0);
}

TEST(osc, ended_by_a_string_terminator) {
    Collected c = run("\x1b]2;name\x1b\\");

    const Action *a = first(c, ActionTag::osc_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(strcmp(a->string, "2;name") == 0);

    /* The ST arrives as what it is: an escape sequence ending in backslash. */
    ASSERT_EQ(count_of(c, ActionTag::esc_dispatch), 1u);
    ASSERT_TRUE(action_is_string_terminator(c.list[c.count - 1]));
}

TEST(osc, an_empty_one) {
    Collected c = run("\x1b]\x07");

    const Action *a = first(c, ActionTag::osc_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->string_len, 0u);
}

TEST(osc, a_very_long_one_is_truncated_not_dropped) {
    Parser p;
    Collected c;

    c.feed(&p, "\x1b]0;");
    for (int i = 0; i < 2000; i++) c.feed(&p, "x");
    c.feed(&p, "\x07");

    /* The useful part of an over-long string is usually at the front. */
    const Action *a = first(c, ActionTag::osc_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(a->string_truncated);
    ASSERT_TRUE(a->string_len > 0u);
}

TEST(osc, returns_to_ground_afterwards) {
    Collected c = run("\x1b]0;t\x07" "after");

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "after") == 0);
}

/* ─── DCS and the strings nobody reads ───────────────────────────────────── */

TEST(dcs, hooks_puts_and_unhooks) {
    Collected c = run("\x1bP1;2q" "data" "\x1b\\");

    const Action *hook = first(c, ActionTag::dcs_hook);
    ASSERT_TRUE(hook != nullptr);
    ASSERT_EQ(hook->final_byte, 'q');
    ASSERT_EQ(hook->param_count, 2);
    ASSERT_EQ(hook->params[1], 2);

    ASSERT_EQ(count_of(c, ActionTag::dcs_put), 4u);
    ASSERT_EQ(count_of(c, ActionTag::dcs_unhook), 1u);
}

TEST(dcs, its_payload_is_not_printed) {
    Collected c = run("\x1bPq" "hidden" "\x1b\\" "shown");

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "shown") == 0);
}

TEST(apc, is_consumed_whole) {
    Collected c = run("\x1b_Gf=100,a=T;payload\x1b\\" "visible");

    /* The alternative to swallowing somebody else's protocol is printing it
     * onto the screen. */
    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "visible") == 0);
}

TEST(apc, does_not_swallow_what_follows) {
    Collected c = run("\x1b^private\x1b\\" "\x1b[2J");

    const Action *a = first(c, ActionTag::csi_dispatch);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->final_byte, 'J');
    ASSERT_EQ(a->params[0], 2);
}

/* ─── nothing leaks between sequences ────────────────────────────────────── */

TEST(parser, one_sequence_does_not_affect_the_next) {
    Collected c = run("\x1b[?1;2;3h" "\x1b[m");

    ASSERT_EQ(count_of(c, ActionTag::csi_dispatch), 2u);

    const Action *second = nullptr;
    size_t seen = 0;
    for (size_t i = 0; i < c.count; i++) {
        if (c.list[i].tag == ActionTag::csi_dispatch && ++seen == 2) {
            second = &c.list[i];
        }
    }

    ASSERT_TRUE(second != nullptr);
    ASSERT_EQ(second->param_count, 0);
    ASSERT_EQ(second->private_marker, 0);
    ASSERT_EQ(second->intermediate_count, 0);
    ASSERT_EQ(second->param_is_sub, 0);
}

TEST(parser, a_realistic_run) {
    Collected c = run("\x1b[H\x1b[2J" "\x1b[1;31m" "error" "\x1b[0m" "\r\n");

    ASSERT_EQ(count_of(c, ActionTag::csi_dispatch), 4u);
    ASSERT_EQ(count_of(c, ActionTag::execute), 2u);

    char got[32];
    printed(c, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "error") == 0);
}
