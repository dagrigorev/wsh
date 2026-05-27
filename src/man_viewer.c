#include "man_viewer.h"
#include "core/str_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static ManViewer g_mv = {0};

void man_viewer_init(void) {
    memset(&g_mv, 0, sizeof(g_mv));
}

const ManViewer *man_viewer_get_state(void) {
    return &g_mv;
}

static char *read_file(const char *path, int *out_len) {
    wchar_t *wpath = u8_to_u16(path, NULL);
    if (!wpath) return NULL;
    FILE *f = _wfopen(wpath, L"rb");
    str_free(wpath);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = (int)n;
    return buf;
}

static int normalize_lines(const char *text, int text_len, char ***out_lines) {
    if (!text || text_len <= 0) { *out_lines = NULL; return 0; }
    char *copy = (char *)malloc((size_t)text_len + 1);
    if (!copy) { *out_lines = NULL; return 0; }
    memcpy(copy, text, (size_t)text_len);
    copy[text_len] = '\0';

    int raw_count = 0;
    for (int i = 0; i < text_len; i++) if (copy[i] == '\n') raw_count++;
    if (text_len > 0 && copy[text_len - 1] != '\n') raw_count++;
    if (raw_count < 1) { free(copy); *out_lines = NULL; return 0; }

    char **raw = (char **)calloc((size_t)raw_count + 1, sizeof(char *));
    if (!raw) { free(copy); *out_lines = NULL; return 0; }

    int raw_idx = 0;
    char *line = copy;
    for (int i = 0; i <= text_len; i++) {
        if (copy[i] == '\n' || copy[i] == '\0') {
            copy[i] = '\0';
            char *end = line + strlen(line);
            if (end > line && *(end - 1) == '\r') *(end - 1) = '\0';
            raw[raw_idx++] = str_dup(line);
            line = copy + i + 1;
        }
    }
    free(copy);

    char **norm = (char **)calloc((size_t)raw_count + 1, sizeof(char *));
    if (!norm) {
        for (int i = 0; i < raw_count; i++) str_free(raw[i]);
        free(raw); *out_lines = NULL; return 0;
    }

    int norm_idx = 0;
    for (int i = 0; i < raw_count; i++) {
        char *rl = raw[i];
        char *end = rl + strlen(rl);
        while (end > rl && (*(end - 1) == ' ' || *(end - 1) == '\t')) end--;
        *end = '\0';

        if (rl[0] == '\0') {
            norm[norm_idx++] = str_dup("");
            str_free(rl);
            continue;
        }

        int leading = 0;
        while (rl[leading] == ' ' || rl[leading] == '\t') leading++;

        char *clean = (char *)malloc(strlen(rl) + 1);
        if (!clean) { str_free(rl); continue; }

        int ci = 0;
        int lead_cap = leading < 8 ? leading : 8;
        for (int li = 0; li < lead_cap; li++) clean[ci++] = ' ';

        bool in_space = true;
        int pos = leading;
        while (rl[pos]) {
            if (rl[pos] == ' ' || rl[pos] == '\t') {
                if (!in_space) { clean[ci++] = ' '; in_space = true; }
            } else {
                clean[ci++] = rl[pos];
                in_space = false;
            }
            pos++;
        }
        if (ci > 0 && clean[ci - 1] == ' ') ci--;
        clean[ci] = '\0';

        norm[norm_idx++] = clean;
        str_free(rl);
    }
    free(raw);
    *out_lines = norm;
    return norm_idx;
}

bool man_viewer_open_topic(const char *topic) {
    if (!topic || !*topic) topic = "wsh";

    man_viewer_close();

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *last_bs = strrchr(path, '\\');
    if (last_bs) *last_bs = '\0';

    char full[MAX_PATH];
    _snprintf(full, MAX_PATH, "%s\\man\\%s.txt", path, topic);

    int text_len = 0;
    char *text = read_file(full, &text_len);

    if (!text) {
        char appdata[MAX_PATH] = {0};
        GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
        if (appdata[0]) {
            _snprintf(full, MAX_PATH, "%s\\Wsh\\man\\%s.txt", appdata, topic);
            text = read_file(full, &text_len);
        }
    }

    strncpy(g_mv.topic, topic, sizeof(g_mv.topic) - 1);
    g_mv.topic[sizeof(g_mv.topic) - 1] = '\0';

    if (!text) return false;

    g_mv.lines = NULL;
    g_mv.line_count = normalize_lines(text, text_len, &g_mv.lines);
    free(text);
    g_mv.scroll_offset = 0;
    g_mv.open = true;
    return true;
}

void man_viewer_close(void) {
    if (g_mv.lines) {
        for (int i = 0; i < g_mv.line_count; i++) str_free(g_mv.lines[i]);
        free(g_mv.lines);
        g_mv.lines = NULL;
    }
    g_mv.line_count = 0;
    g_mv.scroll_offset = 0;
    g_mv.open = false;
    g_mv.topic[0] = '\0';
}

void man_viewer_scroll(int delta) {
    if (!g_mv.open) return;
    g_mv.scroll_offset += delta;
    if (g_mv.scroll_offset < 0) g_mv.scroll_offset = 0;
    int max_offset = g_mv.line_count > 1 ? g_mv.line_count - 1 : 0;
    if (g_mv.scroll_offset > max_offset) g_mv.scroll_offset = max_offset;
}
