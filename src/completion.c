#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "completion.h"
#include "shell.h"
#include "expand.h"
#include "util.h"

/* ─── Internal match list ────────────────────────────────────────────────── */

typedef struct {
    char **items;
    int    count;
    int    cap;
} MatchList;

static void ml_init(MatchList *m) { memset(m, 0, sizeof(*m)); }
static void ml_push(MatchList *m, char *s) {
    if (!s) return;
    if (m->count >= m->cap) {
        m->cap  = m->cap ? m->cap * 2 : 32;
        m->items = (char **)HeapReAlloc(GetProcessHeap(), 0, m->items,
                                         (size_t)m->cap * sizeof(char *));
    }
    m->items[m->count++] = s;
}
static void ml_free(MatchList *m) {
    for (int i = 0; i < m->count; i++) HeapFree(GetProcessHeap(), 0, m->items[i]);
    HeapFree(GetProcessHeap(), 0, m->items);
    memset(m, 0, sizeof(*m));
}

/* ─── Find word being completed ──────────────────────────────────────────── */

/* Returns pointer into line of the start of the current word, and its length */
static int word_start(const char *line, int cursor_pos) {
    int i = cursor_pos;
    while (i > 0 && line[i-1] != ' ' && line[i-1] != '\t' &&
           line[i-1] != '|' && line[i-1] != ';' && line[i-1] != '(')
        i--;
    return i;
}

/* ─── Collect file matches ───────────────────────────────────────────────── */

static void collect_files(const char *prefix, const char *dir, MatchList *m) {
    char search[MAX_PATH];
    _snprintf(search, MAX_PATH, "%s\\%s*", dir[0] ? dir : ".", prefix);
    wchar_t *ws = utf8_to_utf16(search, NULL);
    if (!ws) return;

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(ws, &fd);
    HeapFree(GetProcessHeap(), 0, ws);
    if (hf == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        char *name = utf16_to_utf8(fd.cFileName, NULL);
        if (!name) continue;
        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        /* Build full display string */
        char full[MAX_PATH];
        if (dir[0]) _snprintf(full, MAX_PATH, "%s\\%s%s", dir, name, is_dir ? "\\" : "");
        else        _snprintf(full, MAX_PATH, "%s%s", name, is_dir ? "\\" : "");
        ml_push(m, str_dup(full));
        HeapFree(GetProcessHeap(), 0, name);
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

/* ─── Collect command matches (builtins + PATH) ──────────────────────────── */

static void collect_commands(const char *prefix, const ShellContext *ctx, MatchList *m) {
    /* Built-ins */
    extern const char *builtin_names[]; /* declared via iteration */
    static const char *BUILTIN_NAMES[] = {
        "cd","echo","printf","export","unset","alias","unalias","source",
        "exit","return","true","false","test","read","set","setopt","jobs",
        "fg","bg","kill","wait","pwd","type","which","eval","exec","local",
        "typeset","declare","hash","trap","autoload","compdef","compinit",
        "zstyle","zle","open","clip","env","sudo", NULL
    };
    size_t plen = strlen(prefix);
    for (int i = 0; BUILTIN_NAMES[i]; i++) {
        if (strncmp(BUILTIN_NAMES[i], prefix, plen) == 0)
            ml_push(m, str_dup(BUILTIN_NAMES[i]));
    }

    /* Aliases */
    for (Alias *a = ctx->aliases; a; a = a->next) {
        if (strncmp(a->name, prefix, plen) == 0) ml_push(m, str_dup(a->name));
    }

    /* Shell functions */
    for (ShellFunc *f = ctx->functions; f; f = f->next) {
        if (strncmp(f->name, prefix, plen) == 0) ml_push(m, str_dup(f->name));
    }

    /* PATH search */
    char pathenv[32768] = {0};
    GetEnvironmentVariableA("PATH", pathenv, sizeof(pathenv));
    char **dirs = NULL;
    int ndir = str_split(pathenv, ';', &dirs);
    for (int d = 0; d < ndir; d++) {
        if (!dirs[d] || !dirs[d][0]) continue;
        char search[MAX_PATH];
        _snprintf(search, MAX_PATH, "%s\\%s*.exe", dirs[d], prefix);
        wchar_t *ws = utf8_to_utf16(search, NULL);
        if (!ws) continue;
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(ws, &fd);
        HeapFree(GetProcessHeap(), 0, ws);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            char *name = utf16_to_utf8(fd.cFileName, NULL);
            if (name) {
                /* Strip .exe extension for display */
                char *dot = strrchr(name, '.');
                if (dot && !_stricmp(dot, ".exe")) *dot = '\0';
                ml_push(m, str_dup(name));
                HeapFree(GetProcessHeap(), 0, name);
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    str_split_free(dirs, ndir);
}

/* ─── Compute common prefix of all matches ───────────────────────────────── */

static int common_prefix(char **items, int count) {
    if (count == 0) return 0;
    if (count == 1) return (int)strlen(items[0]);
    int len = 0;
    while (items[0][len]) {
        char c = items[0][len];
        for (int i = 1; i < count; i++) {
            if (items[i][len] != c) return len;
        }
        len++;
    }
    return len;
}

/* ─── Sort / deduplicate matches ─────────────────────────────────────────── */

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void ml_sort_dedup(MatchList *m) {
    if (m->count < 2) return;
    qsort(m->items, (size_t)m->count, sizeof(char *), cmp_str);
    int out = 1;
    for (int i = 1; i < m->count; i++) {
        if (strcmp(m->items[i], m->items[out-1]) != 0) {
            m->items[out++] = m->items[i];
            m->items[i] = NULL;
        } else {
            HeapFree(GetProcessHeap(), 0, m->items[i]);
            m->items[i] = NULL;
        }
    }
    m->count = out;
}

/* ─── completion_compute ─────────────────────────────────────────────────── */

CompletionResult completion_compute(const char *line, int cursor_pos,
                                    const ShellContext *ctx) {
    CompletionResult cr = {0};
    if (!line) return cr;

    int ws = word_start(line, cursor_pos);
    char prefix[MAX_PATH] = {0};
    strncpy(prefix, line + ws, (size_t)(cursor_pos - ws));

    MatchList m; ml_init(&m);

    /* Determine if we are completing a command (first word) or file */
    bool first_word = true;
    for (int i = 0; i < ws; i++) {
        char c = line[i];
        if (c != ' ' && c != '\t') { first_word = false; break; }
    }

    /* Variable completion: $V<TAB> */
    if (prefix[0] == '$') {
        const char *varprefix = prefix + 1;
        size_t vplen = strlen(varprefix);
        wchar_t *envblock = GetEnvironmentStringsW();
        if (envblock) {
            for (wchar_t *p = envblock; *p; p += wcslen(p)+1) {
                char *line8 = utf16_to_utf8(p, NULL);
                if (!line8) continue;
                char *eq = strchr(line8, '=');
                if (eq) {
                    *eq = '\0';
                    if (strncasecmp(line8, varprefix, vplen) == 0) {
                        char full[256]; _snprintf(full, sizeof(full), "$%s", line8);
                        ml_push(&m, str_dup(full));
                    }
                }
                HeapFree(GetProcessHeap(), 0, line8);
            }
            FreeEnvironmentStringsW(envblock);
        }
    } else if (first_word && !strchr(prefix, '/') && !strchr(prefix, '\\')) {
        /* Command completion */
        collect_commands(prefix, ctx, &m);
    } else {
        /* File completion */
        char dir[MAX_PATH] = {0}, file_prefix[MAX_PATH] = {0};
        const char *last_sep = strrchr(prefix, '\\');
        const char *last_fwd = strrchr(prefix, '/');
        const char *sep = last_sep > last_fwd ? last_sep : last_fwd;
        if (sep) {
            strncpy(dir, prefix, (size_t)(sep - prefix));
            strncpy(file_prefix, sep + 1, MAX_PATH-1);
        } else {
            strncpy(file_prefix, prefix, MAX_PATH-1);
        }
        /* Expand tilde in dir */
        char *edir = NULL;
        if (dir[0] == '~') {
            char *home = NULL;
            char profile[MAX_PATH] = {0};
            GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
            home = profile;
            _snprintf(dir, MAX_PATH, "%s%s", home, dir+1);
            edir = str_dup(dir);
        }
        collect_files(file_prefix, dir, &m);
        if (edir) HeapFree(GetProcessHeap(), 0, edir);
    }

    ml_sort_dedup(&m);

    cr.count             = m.count;
    cr.matches           = m.items; /* Transfer ownership */
    cr.common_prefix_len = common_prefix(cr.matches, cr.count);
    cr.selected          = 0;
    cr.menu_active       = (cr.count > 1);

    /* Don't free m.items — transferred to cr */
    return cr;
}

/* ─── completion_free ────────────────────────────────────────────────────── */

void completion_free(CompletionResult *cr) {
    for (int i = 0; i < cr->count; i++) HeapFree(GetProcessHeap(), 0, cr->matches[i]);
    HeapFree(GetProcessHeap(), 0, cr->matches);
    memset(cr, 0, sizeof(*cr));
}

/* ─── completion_apply ───────────────────────────────────────────────────── */

int completion_apply(const CompletionResult *cr, int selected,
                     char *line_buf, int line_len, int cursor_pos, int buf_size) {
    if (!cr || cr->count == 0 || selected < 0 || selected >= cr->count) return cursor_pos;

    int ws = word_start(line_buf, cursor_pos);
    const char *match = cr->matches[selected];
    int match_len = (int)strlen(match);

    /* Build new line: prefix + match + suffix */
    char new_line[8192];
    int new_len = ws;
    memcpy(new_line, line_buf, (size_t)ws);
    int copy_match = match_len < buf_size - ws - 2 ? match_len : buf_size - ws - 2;
    memcpy(new_line + new_len, match, (size_t)copy_match);
    new_len += copy_match;

    /* Append suffix after old word */
    const char *suffix = line_buf + cursor_pos;
    while (*suffix && new_len < buf_size - 1) new_line[new_len++] = *suffix++;
    new_line[new_len] = '\0';

    memcpy(line_buf, new_line, (size_t)new_len + 1);
    return ws + copy_match;
}
