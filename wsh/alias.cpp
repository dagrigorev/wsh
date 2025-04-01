#include "alias.h"

#include <iostream>
#include <sstream>

void AliasManager::AddAlias(const std::string& name, const std::string& value) {
	m_aliases[name] = value;
}

void AliasManager::RemoveAlias(const std::string& name) {
	if (m_aliases.contains(name))
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