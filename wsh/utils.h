#include <string>

bool IsWindows7OrLater();
void GetCurrentWorkDirectory(int max_path, char* cwd);
std::string Colorize(const std::string& text, const std::string& fg, const std::string& bg);
void EnableAnsiColors();
void ReadStdInput(char* ch, unsigned long& chars_read);
std::string GetCurrentDir();