#include "config.h"
#include <iostream>
#include <fstream>

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