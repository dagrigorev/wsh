#pragma once
#ifndef WSH_PHI4_CONFIG_H
#define WSH_PHI4_CONFIG_H

#include <string>

namespace wsh {

struct Phi4Config {
    std::string model_path;
    int context_tokens = 1024;
    int max_tokens = 64;
    float temperature = 0.85f;
};

} /* namespace wsh */

#endif /* WSH_PHI4_CONFIG_H */
