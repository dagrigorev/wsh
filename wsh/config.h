#include <string>
#include <map>
#include <filesystem>

#include "alias.h"

/**
 * Configuration manager
 **/
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