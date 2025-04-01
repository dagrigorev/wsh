#include "config.h"

/**
 * Virtual environment definition 
 **/
class VirtualEnv {
public:
	bool CheckForVirtualEnv(const std::filesystem::path& dir);
	bool IsActive() const { return m_active; }
	const WshConfig::Environment& GetCurrentEnv() const { return m_current_env; }

private:
	bool m_active = false;
	WshConfig m_config;
	WshConfig::Environment m_current_env;
};