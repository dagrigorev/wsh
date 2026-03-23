/*
 * guid_init.c — Define COM GUIDs for Direct2D and DirectWrite.
 *
 * INITGUID must be defined in exactly ONE translation unit before including
 * d2d1.h and dwrite.h so that IID_* symbols get actual definitions rather
 * than extern declarations.  This file is that translation unit.
 */
#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
