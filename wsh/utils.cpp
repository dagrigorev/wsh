#include "utils.h"

bool IsWindows7OrLater() {
    OSVERSIONINFOEX osvi = { sizeof(OSVERSIONINFOEX) };
    DWORDLONG conditionMask = 0;

    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 1;

    return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, conditionMask);
}