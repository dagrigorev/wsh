#include <vector>
#include <string>

#pragma once

class TabCompleter {
public:
	std::vector<std::string> Complete(const std::string& prefix);
};