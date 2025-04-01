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
	else if (command.find("cd ") == 0) {
		HandleCdCommand(const_cast<std::string&>(command));
	}
	else if (command.find("touch ") == 0) {
		HandleTouchCommand(const_cast<std::string&>(command));
	}
	else if (command.find("md ") == 0 || command == "md" ||
		command.find("mkdir ") == 0 || command == "mkdir") {
		HandleMkdirCommand(const_cast<std::string&>(command));
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

void ZWinShell::HandleCdCommand(std::string& command) {
	
	try {
		std::string path = command.substr(3);

		if (path.empty()) {
			// Change to home directory if no path specified
			fs::current_path(fs::path(getenv("USERPROFILE")));
		}
		else {
			// Handle special paths
			if (path == "..") {
				fs::current_path(fs::current_path().parent_path());
			}
			else if (path == "-") {
				// Optional: Implement directory stack for "cd -"
				std::cerr << "cd - not implemented yet\n";
				return;
			}
			else {
				fs::current_path(fs::absolute(path));
			}
		}

		// Update virtual environment and prompt
		UpdateVirtualEnv();
	}
	catch (const fs::filesystem_error& e) {
		std::cerr << "cd: " << e.what() << "\n";
	}
}

void ZWinShell::HandleTouchCommand(std::string& command) {
	// Remove "touch " from the beginning of the command
	size_t touch_pos = command.find("touch ");
	if (touch_pos != std::string::npos) {
		command.erase(touch_pos, 6); // Remove "touch "
	}

	// Trim leading whitespace
	command.erase(0, command.find_first_not_of(" \t"));

	if (command.empty()) {
		std::cerr << "touch: missing file operand\n";
		return;
	}

	// Split filenames by whitespace
	std::istringstream iss(command);
	std::string filename;

	while (iss >> filename) {
		try {
			// Create empty file or update modification time
			std::ofstream file(filename, std::ios::app);
			if (!file) {
				std::cerr << "touch: cannot create file '" << filename << "'\n";
				continue;
			}

			// Update last write time to current time
			auto now = fs::file_time_type::clock::now();
			fs::last_write_time(filename, now);
		}
		catch (const fs::filesystem_error& e) {
			std::cerr << "touch: " << e.what() << "\n";
		}
	}
}

void ZWinShell::HandleMkdirCommand(std::string& command) {
	// Remove command prefix (handles both "md " and "mkdir ")
	size_t cmd_pos = command.find("md ");
	if (cmd_pos != std::string::npos) {
		command.erase(cmd_pos, 3);
	}
	else {
		cmd_pos = command.find("mkdir ");
		if (cmd_pos != std::string::npos) {
			command.erase(cmd_pos, 6);
		}
	}

	// Trim leading whitespace
	command.erase(0, command.find_first_not_of(" \t"));

	if (command.empty()) {
		std::cerr << "mkdir: missing operand\n";
		return;
	}

	// Check for -p flag and get directory path
	bool create_parents = false;
	if (command.find("-p") == 0) {
		create_parents = true;
		command.erase(0, 2); // Remove -p
		command.erase(0, command.find_first_not_of(" \t")); // Trim after -p
	}

	if (command.empty()) {
		std::cerr << "mkdir: missing operand after -p\n";
		return;
	}

	try {
		if (create_parents) {
			fs::create_directories(command);
		}
		else {
			fs::create_directory(command);
		}
	}
	catch (const fs::filesystem_error& e) {
		std::cerr << "mkdir: " << e.what() << "\n";
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