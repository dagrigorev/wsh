#include "history.h"
#include <iostream>

void History::Add(const std::string& command) {
	if (!command.empty() && (this->m_history.empty() || this->m_history.back() != command)) {
		this->m_history.push_back(command);
		if (this->m_history.size() > History::MAX_HISTORY) {
			this->m_history.pop_front(); // Maybe add 
		}
	}

	this->m_position = m_history.size(); // - 1 ???
}

void History::DisplayHistory() {
	if (!this->m_history.empty()) {
		for (const auto& command : this->m_history) {
			std::cout << command << std::endl;
		}
	}
}

std::string History::GetPrevious() {
	if (this->m_position > 0)
		--m_position;
	return this->m_position < this->m_history.size() ? this->m_history[this->m_position] : "";
}

std::string History::GetNext() {
	if (this->m_position < this->m_history.size())
		++m_position;
	return this->m_position < this->m_history.size() ? this->m_history[this->m_position] : "";
}