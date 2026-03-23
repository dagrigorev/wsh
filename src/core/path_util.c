#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "path_util.h"
#include "str_util.h"
#include "log.h"

bool path_exists(const char *path) {
    if (!path) return false;
    wchar_t *w = u8_to_u16(path, NULL);
    if (!w) return false;
    DWORD attr = GetFileAttributesW(w);
    str_free(w);
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool path_is_dir(const char *path) {
    if (!path) return false;
    wchar_t *w = u8_to_u16(path, NULL);
    if (!w) return false;
    DWORD attr = GetFileAttributesW(w);
    str_free(w);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           (attr & FILE_ATTRIBUTE_DIRECTORY);
}

char *path_expand_tilde(const char *path) {
    if (!path) return str_dup("");
    if (path[0] != '~') return str_dup(path);

    /* Get HOME or USERPROFILE */
    char home[MAX_PATH] = {0};
    if (!GetEnvironmentVariableA("HOME", home, MAX_PATH))
        GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH);
    if (!home[0]) return str_dup(path);

    /* Concatenate home + rest */
    const char *rest = path + 1; /* skip ~ */
    if (*rest == '\\' || *rest == '/') rest++; /* skip separator */

    return path_join(home, *rest ? rest : "");
}

char *path_join(const char *base, const char *part) {
    if (!base || !base[0]) return str_dup(part ? part : "");
    if (!part || !part[0]) return str_dup(base);

    size_t bl = strlen(base);
    size_t pl = strlen(part);
    bool needs_sep = (base[bl-1] != '\\' && base[bl-1] != '/');

    char *out = (char *)HeapAlloc(GetProcessHeap(), 0,
                                   bl + (needs_sep ? 1 : 0) + pl + 1);
    if (!out) return NULL;
    char *p = out;
    memcpy(p, base, bl); p += bl;
    if (needs_sep) *p++ = '\\';
    memcpy(p, part, pl); p += pl;
    *p = '\0';
    return out;
}

char *path_basename(const char *path) {
    if (!path) return str_dup("");
    const char *s  = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s2 > s) s = s2;
    return str_dup(s ? s + 1 : path);
}

char *path_dirname(const char *path) {
    if (!path) return str_dup(".");
    const char *s  = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s2 > s) s = s2;
    if (!s) return str_dup(".");
    return str_ndup(path, (size_t)(s - path));
}

bool path_mkdirs(const char *path) {
    if (!path || !*path) return false;
    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = '\0';
            wchar_t *w = u8_to_u16(buf, NULL);
            if (w) { CreateDirectoryW(w, NULL); str_free(w); }
            *p = '\\';
        }
    }
    wchar_t *w = u8_to_u16(buf, NULL);
    if (!w) return false;
    BOOL ok = CreateDirectoryW(w, NULL);
    str_free(w);
    return ok || GetLastError() == ERROR_ALREADY_EXISTS;
}
