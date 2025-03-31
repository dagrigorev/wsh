#include <Windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <deque>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>

#pragma once

namespace fs = std::filesystem;

/**
 Command history class
 **/
class History {
public:
	void Add(const std::string& command);
	std::string GetPrevious();
	std::string GetNext();
	void DisplayHistory();

private:
	static const size_t MAX_HISTORY = 100;
	std::deque<std::string> m_history;
	size_t m_position = 0;
};

class TabCompleter {
public:
	std::vector<std::string> Complete(const std::string& prefix);
};

class AliasManager {
public:
	void AddAlias(const std::string& name, const std::string& value);
	void RemoveAlias(const std::string& name);
	bool IsAlias(const std::string& name) const { return m_aliases.find(name) != m_aliases.end(); }
	std::string ExpandAlias(const std::string& input) const;
	const std::unordered_map<std::string, std::string>& GetAllAliases() const { return m_aliases; };
	void HandleAliasCommand(const std::string& command);
	void ShowAllAliases();
	void ExecuteAlias(const std::string& command);

private:
	std::unordered_map<std::string, std::string> m_aliases;
};

class WshConfig {
public:
	struct Theme {
		std::string name; // theme name
		std::string prompt_fg = "32";  // Green
		std::string prompt_bg = "31";   // Black
		std::string text_fg = "37";    // White
		std::string text_bg = "0";     // Black
		std::string error_fg = "31";   // Red
		std::string dir_fg = "34";     // Blue
		std::string dir_bg = "0";
		std::string highlight_fg = "33"; // Yellow
		std::string prompt_symbol = ">";
		std::string env_prompt_symbol = "#";
	};
	struct Environment {
		std::string hostname;
		std::map<std::string, std::string> variables;
		AliasManager m_alias_manager;
		Theme theme;
	};

	bool LoadConfig(const std::filesystem::path& config_path);
	const std::vector<Environment>& GetEnvironments() const { return m_environments; }

private:
	std::vector<Environment> m_environments;
};

class VirtualEnv {
public:
	bool CheckForVirtualEnv(const fs::path& dir);
	bool IsActive() const { return m_active; }
	const WshConfig::Environment& GetCurrentEnv() const { return m_current_env; }

private:
	bool m_active = false;
	WshConfig m_config;
	WshConfig::Environment m_current_env;
};

/**
 * Main shell class
 **/
class ZWinShell {
public:
	ZWinShell();
	int Run();

private:
	bool m_running = true;
	History m_history;
	TabCompleter m_completer;
	VirtualEnv m_virtual_env;

	void DisplayPrompt();
	std::string ReadInput();
	void ProcessCommand(const std::string& command);
	void ExecuteCommand(const std::string& command);
	void ListDirectory();
	void ShowHistory();
	void ExecuteSystemCommand(const std::string& command);
	void UpdateVirtualEnv();
};