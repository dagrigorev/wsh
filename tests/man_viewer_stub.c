/* Stub implementations of man_viewer for test builds.
 * Tests link wsh_shell (which references man_viewer_open_topic via builtins.c)
 * but do not need actual man-page rendering. */
#include "../src/man_viewer.h"
#include <string.h>

static ManViewer g_mv_stub = {0};

void man_viewer_init(void) { memset(&g_mv_stub, 0, sizeof(g_mv_stub)); }

bool man_viewer_open_topic(const char *topic) {
    (void)topic;
    return false;
}

void man_viewer_close(void) {
    g_mv_stub.open = false;
}

void man_viewer_scroll(int delta) {
    (void)delta;
}

const ManViewer *man_viewer_get_state(void) {
    return &g_mv_stub;
}
