# Wsh — nmake build file
# Usage: Open "Developer Command Prompt for VS" then run: nmake
#        Or: nmake DEBUG=1       (debug build)
#            nmake clean         (clean build artifacts)
#            nmake install       (copy to %LOCALAPPDATA%\Wsh\)

CC      = cl.exe
RC      = rc.exe
LINK    = link.exe

# ── Compiler flags ────────────────────────────────────────────────────────────
!IFDEF DEBUG
OPTFLAGS = /Od /Zi /MDd
DBGFLAGS = /DDEBUG /D_DEBUG
LDBG     = /DEBUG
!ELSE
OPTFLAGS = /O2 /GL
DBGFLAGS =
LDBG     =
!ENDIF

CFLAGS = /nologo /W3 /WX- /std:c11 \
         /D_UNICODE /DUNICODE /DWIN32_LEAN_AND_MEAN \
         /D_CRT_SECURE_NO_WARNINGS \
         $(OPTFLAGS) $(DBGFLAGS) \
         /Isrc

# ── Linker flags ──────────────────────────────────────────────────────────────
LDFLAGS = /nologo /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup $(LDBG) \
          kernel32.lib user32.lib gdi32.lib shell32.lib \
          d2d1.lib dwrite.lib \
          advapi32.lib shlwapi.lib netapi32.lib \
          ole32.lib oleaut32.lib \
          /LTCG

# ── Source files ──────────────────────────────────────────────────────────────
SRCS = \
    src\guid_init.c \
    src\main.c      \
    src\window.c    \
    src\renderer.c  \
    src\screen.c    \
    src\vt_parser.c \
    src\input.c     \
    src\shell.c     \
    src\builtins.c  \
    src\history.c   \
    src\completion.c \
    src\expand.c    \
    src\jobs.c      \
    src\pty.c       \
    src\config.c    \
    src\font.c      \
    src\util.c

OBJS = \
    src\guid_init.obj \
    src\main.obj      \
    src\window.obj    \
    src\renderer.obj  \
    src\screen.obj    \
    src\vt_parser.obj \
    src\input.obj     \
    src\shell.obj     \
    src\builtins.obj  \
    src\history.obj   \
    src\completion.obj \
    src\expand.obj    \
    src\jobs.obj      \
    src\pty.obj       \
    src\config.obj    \
    src\font.obj      \
    src\util.obj

RES  = res\Wsh.res
TARGET = Wsh.exe

# ── Default target ────────────────────────────────────────────────────────────
all: $(TARGET)
    @echo.
    @echo Build successful: $(TARGET)

$(TARGET): $(OBJS) $(RES)
    $(LINK) $(LDFLAGS) /OUT:$(TARGET) $(OBJS) $(RES)

# ── Per-file compilation ──────────────────────────────────────────────────────
src\guid_init.obj: src\guid_init.c
    $(CC) $(CFLAGS) /c src\guid_init.c /Fosrc\guid_init.obj

src\main.obj: src\main.c src\util.h src\config.h src\screen.h src\vt_parser.h \
              src\renderer.h src\input.h src\shell.h src\pty.h src\history.h \
              src\completion.h src\window.h
    $(CC) $(CFLAGS) /c src\main.c /Fosrc\main.obj

src\window.obj: src\window.c src\window.h src\util.h
    $(CC) $(CFLAGS) /c src\window.c /Fosrc\window.obj

src\renderer.obj: src\renderer.c src\renderer.h src\screen.h src\config.h src\font.h src\util.h
    $(CC) $(CFLAGS) /c src\renderer.c /Fosrc\renderer.obj

src\screen.obj: src\screen.c src\screen.h src\util.h
    $(CC) $(CFLAGS) /c src\screen.c /Fosrc\screen.obj

src\vt_parser.obj: src\vt_parser.c src\vt_parser.h src\screen.h src\util.h
    $(CC) $(CFLAGS) /c src\vt_parser.c /Fosrc\vt_parser.obj

src\input.obj: src\input.c src\input.h
    $(CC) $(CFLAGS) /c src\input.c /Fosrc\input.obj

src\shell.obj: src\shell.c src\shell.h src\builtins.h src\expand.h src\util.h
    $(CC) $(CFLAGS) /c src\shell.c /Fosrc\shell.obj

src\builtins.obj: src\builtins.c src\builtins.h src\shell.h src\expand.h src\util.h
    $(CC) $(CFLAGS) /c src\builtins.c /Fosrc\builtins.obj

src\history.obj: src\history.c src\history.h src\util.h
    $(CC) $(CFLAGS) /c src\history.c /Fosrc\history.obj

src\completion.obj: src\completion.c src\completion.h src\shell.h src\expand.h src\util.h
    $(CC) $(CFLAGS) /c src\completion.c /Fosrc\completion.obj

src\expand.obj: src\expand.c src\expand.h src\shell.h src\util.h
    $(CC) $(CFLAGS) /c src\expand.c /Fosrc\expand.obj

src\jobs.obj: src\jobs.c src\jobs.h src\util.h
    $(CC) $(CFLAGS) /c src\jobs.c /Fosrc\jobs.obj

src\pty.obj: src\pty.c src\pty.h src\util.h
    $(CC) $(CFLAGS) /c src\pty.c /Fosrc\pty.obj

src\config.obj: src\config.c src\config.h src\util.h
    $(CC) $(CFLAGS) /c src\config.c /Fosrc\config.obj

src\font.obj: src\font.c src\font.h src\util.h
    $(CC) $(CFLAGS) /c src\font.c /Fosrc\font.obj

src\util.obj: src\util.c src\util.h
    $(CC) $(CFLAGS) /c src\util.c /Fosrc\util.obj

# ── Resource compilation ──────────────────────────────────────────────────────
$(RES): res\Wsh.rc
    $(RC) /nologo /fo$(RES) res\Wsh.rc

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
    @if exist src\*.obj del /Q src\*.obj
    @if exist res\*.res del /Q res\*.res
    @if exist $(TARGET) del /Q $(TARGET)
    @if exist Wsh.pdb   del /Q Wsh.pdb
    @if exist Wsh.ilk   del /Q Wsh.ilk
    @echo Clean done.

# ── Install ───────────────────────────────────────────────────────────────────
install: $(TARGET)
    @set DEST=%LOCALAPPDATA%\Wsh
    @if not exist "%LOCALAPPDATA%\Wsh" mkdir "%LOCALAPPDATA%\Wsh"
    copy /Y $(TARGET) "%LOCALAPPDATA%\Wsh\$(TARGET)"
    @echo Installed to %LOCALAPPDATA%\Wsh\$(TARGET)
    @echo Run install.bat to register as default terminal.

.PHONY: all clean install
