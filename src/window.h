#pragma once
#ifndef WSH_WINDOW_H
#define WSH_WINDOW_H

#include <windows.h>
#include <stdbool.h>
#include "platform/config.h"


#ifdef __cplusplus
extern "C" {
#endif

/* Register the Wsh window class. Must be called once before CreateWindow. */
bool window_register_class(HINSTANCE hInst);

/* Create the main terminal window. Returns NULL on failure. */
HWND window_create(HINSTANCE hInst, const Config *cfg, int nShow);

/* Returns true if any background jobs are running (used by WM_CLOSE confirm). */
bool window_has_running_jobs(HWND hwnd);


#ifdef __cplusplus
}
#endif

#endif /* WSH_WINDOW_H */
