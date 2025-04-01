#include "completer.h"
#include <filesystem>

namespace fs = std::filesystem;

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