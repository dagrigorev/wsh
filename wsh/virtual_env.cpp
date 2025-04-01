#include "virtual_env.h"

namespace fs = std::filesystem;

bool VirtualEnv::CheckForVirtualEnv(const fs::path& dir) {
	fs::path config_path = dir / ".wshrc";
	if (!m_config.LoadConfig(config_path)) {
		m_active = false;
		return false;
	}

	const auto& envs = m_config.GetEnvironments();
	if (!envs.empty()) {
		m_current_env = envs[0]; // Use first environment for simplicity
		m_active = true;
		return true;
	}

	m_active = false;
	return false;
}