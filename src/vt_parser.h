#pragma once
#ifndef WSH_VT_PARSER_H
#define WSH_VT_PARSER_H

#include <windows.h>
#include "wsh_bool.h"
#include <stdint.h>
#include "screen.h"

/* ─── Parser States ──────────────────────────────────────────────────────── */

typedef enum {
    VT_GROUND,
    VT_ESCAPE,
    VT_ESCAPE_INTERMEDIATE,
    VT_CSI_ENTRY,
    VT_CSI_PARAM,
    VT_CSI_INTERMEDIATE,
    VT_CSI_IGNORE,
    VT_OSC_STRING,
    VT_DCS_STRING,
    VT_SS2,
    VT_SS3,
} VtState;

/* ─── Parser Context ─────────────────────────────────────────────────────── */

#define VT_MAX_PARAMS   16
#define VT_MAX_OSC      512
#define VT_UTF8_BUF     4

typedef struct {
    VtState   state;
    ScreenBuffer *screen;  /* Target screen buffer */

    /* CSI parameter accumulation */
    int       params[VT_MAX_PARAMS];
    int       param_count;
    bool      question_mark; /* ?-prefixed private sequences */
    bool      bang;          /* !-prefixed sequences */
    char      intermediate;  /* single intermediate char */

    /* OSC/DCS string accumulation */
    char      osc_buf[VT_MAX_OSC];
    int       osc_len;

    /* UTF-8 multi-byte accumulation */
    uint8_t   utf8_buf[VT_UTF8_BUF];
    int       utf8_rem;   /* remaining continuation bytes expected */
    uint32_t  utf8_cp;    /* partial codepoint being assembled */

    /* Character set state */
    int       charset_g0; /* 0 = ASCII, 1 = DEC Special Graphics */
    int       charset_g1;
    bool      use_g1;

    /* Window title callback (set by owner) */
    void    (*on_title)(const char *title, void *userdata);
    void     *userdata;
} VtParser;

/* ─── API ────────────────────────────────────────────────────────────────── */

void vt_parser_init(VtParser *vt, ScreenBuffer *screen);
void vt_parser_feed(VtParser *vt, const char *data, int len);
void vt_parser_reset(VtParser *vt);

#endif /* WSH_VT_PARSER_H */
