#include "utils.h"
#include <istream>
#include <sstream>
#include <iostream>
#include <string>
#include <Windows.h>

bool IsWindows7OrLater() {
    OSVERSIONINFOEX osvi = { sizeof(OSVERSIONINFOEX) };
    DWORDLONG conditionMask = 0;

    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 1;

    return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, conditionMask);
}

/**
  GetCurrentDirectoryA wrapped method
 **/
void GetCurrentWorkDirectory(int max_path, char* cwd) {
	GetCurrentDirectoryA(max_path, cwd);
}

std::string Colorize(const std::string& text, const std::string& fg, const std::string& bg) {
	return "\033[" + bg + ";" + fg + "m" + text + "\033[0m";
}

void EnableAnsiColors() {
	// Enable VT processing on Windows 10+
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) return;

	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode)) return;

	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);
}

void ReadStdInput(char* ch, unsigned long& chars_read) {
	*ch = std::cin.get();
	chars_read = (unsigned long)std::cin.gcount();
	//ReadConsole(GetStdHandle(STD_INPUT_HANDLE), ch, 1, &chars_read, NULL);
}

std::string GetCurrentDir() {
	char cwd[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, cwd);
	return cwd;
}