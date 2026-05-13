# Unicode / Cyrillic compatibility notes

This pass ports the zsh multibyte model into WSH in a Windows-native way.

## zsh concept

zsh keeps shell input as byte strings, but whenever the editor, completion or
refresh logic needs character semantics it interprets bytes through the active
multibyte locale. Important consequences:

- command text is not treated as one byte equals one character;
- cursor movement and deletion operate on complete multibyte characters;
- prompt refresh uses display columns, not byte length;
- invalid byte streams are handled defensively instead of crashing the editor.

## WSH implementation

WSH now uses this contract:

- internal shell text: UTF-8;
- Win32 API boundaries: UTF-16;
- terminal screen cells: Unicode codepoints;
- fallback decoding for legacy Windows child-process output: OEM code page first, then ACP.

The new `src/core/unicode.c` layer centralises these rules.

## Fixed areas

1. REPL redraw now calculates cursor repositioning using display columns. This
   fixes Cyrillic input such as `привет`: each Cyrillic character is two UTF-8
   bytes but one terminal column.
2. Left/right/delete/backspace now move over complete UTF-8 characters.
3. `WM_CHAR` input combines UTF-16 surrogate pairs before encoding to UTF-8.
4. Clipboard paste prefers `CF_UNICODETEXT` and converts UTF-16 to UTF-8.
5. Clipboard copy writes `CF_UNICODETEXT`, so selected Cyrillic text is not lost.
6. External process captured output is normalised to UTF-8. If bytes are already
   valid UTF-8 they pass through; otherwise WSH tries the Windows OEM code page
   and then the ANSI code page.
7. `source ~/.zshrc` reads scripts as bytes, strips UTF-8 BOM, and normalises
   non-UTF-8 legacy files to UTF-8 before parsing.
8. The process initializes a UTF-8-oriented environment by setting console CPs
   and default `LANG` / `LC_CTYPE` values when they are absent.

## Known limitations

- Full grapheme cluster editing is not implemented yet. Combining marks are
  recognised for width calculation, but composition with the previous cell is a
  later ZLE-level enhancement.
- Child-process output encoding cannot always be detected perfectly. The current
  heuristic is safe for common Windows/Russian cases: UTF-8, CP866, CP1251/ACP.
- ConPTY applications are expected to behave as UTF-8-capable terminal apps. The
  fallback decoder is used for captured built-in-shell child processes.
