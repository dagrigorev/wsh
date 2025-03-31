#include "zwinshell.h"

namespace fs = std::filesystem;
using namespace std;

void GetCurrentWorkDirectory(int max_path, char* cwd);
void ReadStdInput(char* ch, unsigned long& chars_read);
void EnableAnsiColors();
std::string GetCurrentDir();
std::string Colorize(const std::string& text, const std::string& fg, const std::string& bg = "0");

ZWinShell::ZWinShell() {
	EnableAnsiColors();
}

int ZWinShell::Run() {
	while (this->m_running) {
		try
		{
			this->UpdateVirtualEnv();

			this->DisplayPrompt();
			auto input = this->ReadInput();
			if (!input.empty()) {
				auto env = m_virtual_env.GetCurrentEnv();

				input = env.m_alias_manager.ExpandAlias(input);

				this->ProcessCommand(input);
				this->m_history.Add(input); // Add to history onlyt after suzzess execute
			}
		}
		catch (const char* error_message) {
			std::cout << error_message << std::endl;
			return -1;
		}
	}

	return 0;
}

void ZWinShell::DisplayPrompt() {
	char current_work_dir[MAX_PATH];
	GetCurrentWorkDirectory(MAX_PATH, current_work_dir);
	auto env = m_virtual_env.GetCurrentEnv();

	if (m_virtual_env.IsActive()) {
		std::cout << Colorize("(" + env.hostname + ")$# ", env.theme.prompt_fg, env.theme.prompt_bg);
		return;
	}

	std::cout << "\033[32m" << current_work_dir << "#" << "\033[0m ";
}

std::string ZWinShell::ReadInput() {
	std::string input;
	char ch = 0;
	unsigned long chars_read = 0;

	while (true) {
		ReadStdInput(&ch, chars_read);

		if (ch == '\r') {  // Enter key
			std::cout << std::endl;
			return input;
		}
		else if (ch == '\n') {
			return input;
		}
		else if (ch == '\t') {  // Tab key
			auto completions = m_completer.Complete(input);
			if (!completions.empty()) {
				// For simplicity, just use the first completion
				input = completions[0];
				// Redraw line
				std::cout << "\r\033[K" << "\033[32m" << GetCurrentDir() << ">\033[0m " << input;
			}
		}
		else if (ch == 0xE0) {  // Arrow key prefix
			ReadStdInput(&ch, chars_read);
			if (ch == 0x48) {  // Up arrow
				input = m_history.GetPrevious();
				std::cout << "\r\033[K" << "\033[32m" << GetCurrentDir() << ">\033[0m " << input;
			}
			else if (ch == 0x50) {  // Down arrow
				input = m_history.GetNext();
				std::cout << "\r\033[K" << "\033[32m" << GetCurrentDir() << ">\033[0m " << input;
			}
		}
		else if (ch == '\b') {  // Backspace
			if (!input.empty()) {
				input.pop_back();
				std::cout << "\b \b";
			}
		}
		else if (ch >= 32 && ch <= 126) {  // Printable characters
			input += ch;
            // std::cout << ch;
		}
	}
}

void ZWinShell::ProcessCommand(const std::string& command) {
	if (command.empty()) return;
	
	auto env = m_virtual_env.GetCurrentEnv();

	// TODO: Recreate command auto searching and executing. Make alias supports here.
	if (command.find("alias ") == 0) {
		env.m_alias_manager.HandleAliasCommand(command);
		return;
	}
	else if (command.find("aliases") == 0) {
		// Show all aliases
		env.m_alias_manager.ShowAllAliases();
		return;
	}else if (command == "exit") {
		m_running = false;
	}
	else if (command == "pwd") {
		std::cout << GetCurrentDir() << std::endl;
	}
	else if (command == "ls" || command == "dir") {
		this->ListDirectory();
	}
	else if (command == "history") {
		this->ShowHistory();
	}
	else {
		this->ExecuteSystemCommand(command);
	}
}

void ZWinShell::ListDirectory() {
	try {
		for (const auto& entry : fs::directory_iterator(".")) {
			if (fs::is_directory(entry.status())) {
				std::cout << "\033[34m" << entry.path().filename().string() << "\033[0m" << std::endl;
			}
			else if(fs::is_regular_file(entry.status())){
				std::cout << entry.path().filename().string() << std::endl;
			}
		}
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
}

void ZWinShell::ShowHistory() {
	m_history.DisplayHistory();
}

void ZWinShell::ExecuteSystemCommand(const std::string& command) {
	std::cout << command << std::endl;
	system(command.c_str());
}

void ZWinShell::UpdateVirtualEnv() {
	fs::path current_dir = fs::current_path();
	if (m_virtual_env.CheckForVirtualEnv(current_dir)) {
		const auto& env = m_virtual_env.GetCurrentEnv();
		for (const auto& [key, value] : env.variables) {
			std::string env_var = key + "=" + value;
			_putenv(env_var.c_str());
		}
	}
}

void ZWinShell::ExecuteCommand(const std::string& command) {
	if (command == "exit") {
		char current_working_dir[MAX_PATH];
		GetCurrentWorkDirectory(MAX_PATH, current_working_dir);
		std::cout << current_working_dir << std::endl;
	}
	else if (command == "ls" && command == "dir") {
		for (const auto& entry : fs::directory_iterator(".")) {
			std::cout << entry.path().filename().string() << std::endl;
		}
	}
	else {
		// Fallback to system command
		system(command.c_str());
	}
}

void History::Add(const std::string& command) {
	if (!command.empty() && (this->m_history.empty() || this->m_history.back() != command)) {
		this->m_history.push_back(command);
		if (this->m_history.size() > History::MAX_HISTORY) {
			this->m_history.pop_front(); // Maybe add 
		}
	}

	this->m_position = m_history.size(); // - 1 ???
}

void History::DisplayHistory() {
	if (!this->m_history.empty()) {
		for (const auto& command : this->m_history) {
			std::cout << command << std::endl;
		}
	}
}

std::string History::GetPrevious() {
	if (this->m_position > 0)
		--m_position;
	return this->m_position < this->m_history.size() ? this->m_history[this->m_position] : "";
}

std::string History::GetNext() {
	if (this->m_position < this->m_history.size())
		++m_position;
	return this->m_position < this->m_history.size() ? this->m_history[this->m_position] : "";
}

std::vector<std::string> TabCompleter::Complete(const std::string& prefix) {
	std::vector<std::string> matches;

	try {
		fs::path parent = fs::path(prefix).parent_path();
		if (parent.empty()) parent = ".";

		std::string stem = fs::path(prefix).filename().string();

		for (const auto& entry : fs::directory_iterator(parent)) {
			std::string name = entry.path().filename().string();
			if (name.find(stem) == 0) {
				matches.push_back((parent / name).string());
			}
		}
	}
	catch (...) {
		// Ignore fs errors
	}

	return matches;
}

bool VirtualEnv::CheckForVirtualEnv(const fs::path& dir) {
	fs::path config_path = dir / ".wshrc";
	if (!m_config.LoadConfig(config_path)) {
		m_active = false;
		return false;
	}

	const auto& envs = m_config.GetEnvironments();
	if (!envs.empty()) {
		m_current_env = envs[0]; // Use first environment for simplicity
		m_active = true;
		return true;
	}

	m_active = false;
	return false;
}

bool WshConfig::LoadConfig(const std::filesystem::path& config_path) {
	if (!std::filesystem::exists(config_path)) {
		return false;
	}

	std::ifstream file(config_path);
	std::string line;
	Environment current_env;
	bool in_env_section = false;
	bool in_theme_section = false;

	m_environments.clear(); // Remove old envs to load updated from file

	while (getline(file, line)) {
		// Remove comments
		size_t comment_pos = line.find('#');
		if (comment_pos != std::string::npos) {
			line = line.substr(0, comment_pos);
		}

		// Trim whitespace
		line.erase(0, line.find_first_not_of(" \t"));
		line.erase(line.find_last_not_of(" \t") + 1);

		if (line.empty()) continue;

		if (line.find("env ") == 0) {
			// Environment declaration
			if (in_env_section) {
				m_environments.push_back(current_env);
			}
			current_env = Environment();
			current_env.hostname = line.substr(4);
			in_env_section = true;
		}
		else if (in_env_section && line.find("theme ") == 0) {
			current_env.theme.name = line.substr(6, line.find(':'));
			in_theme_section = true;
		}
		else if (in_env_section && in_theme_section) {
			size_t eq_pos = line.find('=');
			std::string key = line.substr(0, eq_pos);
			std::string value = line.substr(eq_pos + 1);

			// Trim whitespace from value
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t") + 1);

			// Handle theme settings
			if (key == "prompt_fg") current_env.theme.prompt_fg = value;
			else if (key == "prompt_bg") current_env.theme.prompt_bg = value;
			else if (key == "text_fg") current_env.theme.text_fg = value;
			else if (key == "text_bg") current_env.theme.text_bg = value;
			else if (key == "error_fg") current_env.theme.error_fg = value;
			else if (key == "dir_fg") current_env.theme.dir_fg = value;
			else if (key == "dir_bg") current_env.theme.dir_bg = value;
			else if (key == "highlight_fg") current_env.theme.highlight_fg = value;
			else if (key == "prompt_symbol") current_env.theme.prompt_symbol = value;
			else if (key == "env_prompt_symbol") current_env.theme.env_prompt_symbol = value;
		}
		else if (in_env_section && in_theme_section && (line[0] == '\n' || line[0] == '\r') || line.find("theme ") == 0) {
			in_theme_section = false;
		}
		else if (in_env_section && line.find("alias") == 0) {
			// Alias declaration
			size_t equal_pos = line.find('=', 6);
			if (equal_pos != std::string::npos) {
				std::string name = line.substr(6, equal_pos - 6);
				std::string value = line.substr(equal_pos + 1);

				// Trim whitespace from name and value
				name.erase(name.find_last_not_of(" \t") + 1);
				value.erase(0, value.find_first_not_of(" \t"));
				value.erase(value.find_last_not_of(" \t") + 1);

				// Remove quotes if present
				if (value.front() == '\'' || value.front() == '"') {
					value = value.substr(1, value.length() - 2);
				}

				current_env.m_alias_manager.AddAlias(name, value);
			}
		}
		else if (in_env_section && line.find('=') != std::string::npos) {
			// Variable assignment
			size_t eq_pos = line.find('=');
			std::string key = line.substr(0, eq_pos);
			std::string value = line.substr(eq_pos + 1);
			current_env.variables[key] = value;
		}
	}

	if (in_env_section) {
		m_environments.push_back(current_env);
	}

	return !m_environments.empty();
}

void AliasManager::AddAlias(const std::string& name, const std::string& value) {
	m_aliases[name] = value;
}

void AliasManager::RemoveAlias(const std::string& name) {
	if(m_aliases.contains(name))
		m_aliases.erase(name);
}

std::string AliasManager::ExpandAlias(const std::string& input) const {
	std::istringstream is(input);
	std::string first_word;
	is >> first_word;

	if (!IsAlias(first_word)) {
		return input;
	}

	std::string expanded = m_aliases.at(first_word);
	std::string remaining;
	std::getline(is, remaining);

	remaining.erase(0, remaining.find_first_not_of(" \t"));

	if (!remaining.empty()) {
		expanded += " " + remaining;
	}

	return expanded;
}

void AliasManager::HandleAliasCommand(const std::string& command) {
	std::istringstream iss(command.substr(6)); // Skip "alias "
	std::string name;
	iss >> name;

	if (name.empty()) {
		ShowAllAliases();
		return;
	}

	// Check for alias removal (unalias)
	if (name == "-r" || name == "--remove") {
		std::string alias_to_remove;
		iss >> alias_to_remove;
		if (!alias_to_remove.empty()) {
			RemoveAlias(alias_to_remove);
			std::cout << "Removed alias: " << alias_to_remove << std::endl;
		}
		return;
	}

	// Check for equals sign in the rest of the command
	std::string remaining;
	std::getline(iss, remaining);
	size_t equal_pos = remaining.find('=');

	if (equal_pos != std::string::npos) {
		// alias name=value
		std::string value = remaining.substr(equal_pos + 1);
		value.erase(0, value.find_first_not_of(" \t"));
		value.erase(value.find_last_not_of(" \t") + 1);

		// Remove surrounding quotes if present
		if ((value.front() == '\'' && value.back() == '\'') ||
			(value.front() == '"' && value.back() == '"')) {
			value = value.substr(1, value.length() - 2);
		}

		AddAlias(name, value);
		std::cout << "Added alias: " << name << "='" << value << "'" << std::endl;
	}
	else {
		// Just showing an alias
		if (IsAlias(name)) {
			std::cout << name << "='" << ExpandAlias(name) << "'" << std::endl;
		}
		else {
			std::cout << "Alias not found: " << name << std::endl;
		}
	}
}

void AliasManager::ShowAllAliases() {
	const auto& aliases = GetAllAliases();
	if (aliases.empty()) {
		std::cout << "No aliases defined" << std::endl;
		return;
	}

	size_t max_name_length = 0;
	for (const auto& [name, value] : aliases) {
		if (name.length() > max_name_length) {
			max_name_length = name.length();
		}
	}

	for (const auto& [name, value] : aliases) {
		std::cout << "  " << name << std::string(max_name_length - name.length() + 2, ' ')
			<< "'" << value << "'" << std::endl;
	}
}

// Platform specific methods called wrapped

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

template <size_t N>
istream& read_word(istream& is, char (&buffer)[N], int max)
    // read at most max-1 characters from is into buffer
{
    is.width(max);    // read at most max-1 characters in the next >>
    is >> buffer;     // read whitespace-terminated word,
        // add zero after the last character read into buffer
    return is;
}

void ReadStdInput(char* ch, unsigned long& chars_read) {
	*ch = std::cin.get();
	chars_read = std::cin.gcount();
	//ReadConsole(GetStdHandle(STD_INPUT_HANDLE), ch, 1, &chars_read, NULL);
}

std::string GetCurrentDir() {
	char cwd[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, cwd);
	return cwd;
}
