#include <string>
#include <deque>

#pragma once

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