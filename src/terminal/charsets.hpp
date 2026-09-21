/* Reimplemented after Ghostty src/terminal/charsets.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * REIMPLEMENTED, NOT TRANSLITERATED. This file was written from Ghostty's
 * design and verified against Wisp's own tests, without the upstream source
 * to hand. It follows upstream's structure but has not been checked against
 * it line by line, and its behaviour will differ in places. It is due to be
 * replaced by a transliteration checked against upstream's own tests, as
 * parser.hpp has been.
 *
 * The 7-bit character sets a terminal can be told to substitute.
 *
 * Before Unicode a terminal had no way to draw a box, so DEC defined a set
 * that reused the lowercase letters for line-drawing pieces: switch to it,
 * send "lqqk", and a corner, two horizontal lines and another corner appear.
 * Programs still do this. ncurses does it by default when it cannot be sure
 * the terminal handles UTF-8, which is why a TUI that draws its borders as
 * rows of q and x has a terminal that ignored the switch.
 *
 * Only the sets anything still uses are here. The National Replacement
 * Character Sets that swapped a few punctuation marks for accented letters
 * are part of the standard and not of anyone's practice; British is kept
 * because it is the one that replaces a single character and is still
 * occasionally asked for.
 */

#pragma once
#ifndef WISP_TERMINAL_CHARSETS_HPP
#define WISP_TERMINAL_CHARSETS_HPP

#include <stdint.h>

namespace wisp {
namespace terminal {

enum class Charset : uint8_t {
    /* No substitution. Named ASCII by the standard, but with UTF-8 decoded
     * above this it means "print what arrived". */
    ascii = 0,

    /* DEC Special Graphics: the line-drawing set. */
    dec_special,

    /* UK: the same as ASCII except that # is a pound sign. */
    british,
};

/* The designator byte in ESC ( X and its relatives, to the set it names.
 * Returns false for a set nothing here implements, which the caller treats
 * as "leave the designation alone" — substituting ASCII for a set a program
 * asked for would print the wrong characters with no sign anything was
 * wrong. */
inline bool charset_from_designator(uint8_t b, Charset *out) {
    switch (b) {
        case 'B': *out = Charset::ascii; return true;
        case '0': *out = Charset::dec_special; return true;
        case 'A': *out = Charset::british; return true;
        default: return false;
    }
}

/* DEC Special Graphics from 0x5F to 0x7E, as xterm maps it. */
static const uint32_t DEC_SPECIAL[32] = {
    0x00A0, /* _  no-break space */
    0x25C6, /* `  diamond */
    0x2592, /* a  checkerboard */
    0x2409, /* b  HT symbol */
    0x240C, /* c  FF symbol */
    0x240D, /* d  CR symbol */
    0x240A, /* e  LF symbol */
    0x00B0, /* f  degree */
    0x00B1, /* g  plus-minus */
    0x2424, /* h  NL symbol */
    0x240B, /* i  VT symbol */
    0x2518, /* j  lower right corner */
    0x2510, /* k  upper right corner */
    0x250C, /* l  upper left corner */
    0x2514, /* m  lower left corner */
    0x253C, /* n  crossing lines */
    0x23BA, /* o  scan line 1 */
    0x23BB, /* p  scan line 3 */
    0x2500, /* q  horizontal line */
    0x23BC, /* r  scan line 7 */
    0x23BD, /* s  scan line 9 */
    0x251C, /* t  left tee */
    0x2524, /* u  right tee */
    0x2534, /* v  bottom tee */
    0x252C, /* w  top tee */
    0x2502, /* x  vertical line */
    0x2264, /* y  less than or equal */
    0x2265, /* z  greater than or equal */
    0x03C0, /* {  pi */
    0x2260, /* |  not equal */
    0x00A3, /* }  pound */
    0x00B7, /* ~  centred dot */
};

/* What a codepoint becomes in a set.
 *
 * Only the 7-bit range is ever touched. Everything above it arrived as UTF-8
 * and means exactly what it says; a program that sends a box-drawing
 * character directly has already done the substitution itself, and mapping
 * it again would be wrong. */
inline uint32_t charset_map(Charset cs, uint32_t cp) {
    if (cp >= 0x80) return cp;

    switch (cs) {
        case Charset::ascii:
            return cp;

        case Charset::dec_special:
            if (cp >= 0x5F && cp <= 0x7E) return DEC_SPECIAL[cp - 0x5F];
            return cp;

        case Charset::british:
            return cp == '#' ? 0x00A3 : cp;
    }
    return cp;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_CHARSETS_HPP */
