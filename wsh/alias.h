#include <string>
#include <unordered_map>

#pragma once

/**
 * Aliases manager
 **/
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