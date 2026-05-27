#pragma once
#ifndef WSH_MAN_VIEWER_H
#define WSH_MAN_VIEWER_H

#include <windows.h>
#include "wsh_bool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAN_VIEWER_COLS 80

typedef struct {
    bool    open;
    char  **lines;
    int     line_count;
    int     scroll_offset;
    char    topic[256];
} ManViewer;

void            man_viewer_init(void);
bool            man_viewer_open_topic(const char *topic);
void            man_viewer_close(void);
void            man_viewer_scroll(int delta);
const ManViewer *man_viewer_get_state(void);

#ifdef __cplusplus
}
#endif

#endif
