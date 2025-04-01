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

#include "history.h"
#include "completer.h"
#include "virtual_env.h"

#pragma once

namespace fs = std::filesystem;

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
	void HandleCdCommand(std::string& command);
	void HandleTouchCommand(std::string& command);
	void HandleMkdirCommand(std::string& command);

	void ShowHistory();
	void ExecuteSystemCommand(const std::string& command);
	void UpdateVirtualEnv();
};