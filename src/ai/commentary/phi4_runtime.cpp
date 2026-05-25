#include "phi4_runtime.h"
#include "../../core/log.h"
#include <windows.h>

namespace wsh {

/* ─── StubPhi4Runtime ───────────────────────────────────────────────────────── */

StubPhi4Runtime::StubPhi4Runtime() {}

StubPhi4Runtime::~StubPhi4Runtime() { Shutdown(); }

bool StubPhi4Runtime::Initialize(const Phi4Config& config) {
    config_ = config;
    initialized_ = true;

    /* Try to detect if a model file exists at the configured path.
     * The stub does NOT load any model — it only checks for file presence.
     * A real runtime would load the GGUF file via llama.cpp. */
    std::string path = config_.model_path;
    if (!path.empty()) {
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            WSH_LOG_INFO("Phi-4 model found at: %s", path.c_str());
        } else {
            WSH_LOG_WARN("Phi-4 model not found at: %s", path.c_str());
        }
    }

    return true;
}

bool StubPhi4Runtime::IsAvailable() const {
    /* Stub always reports unavailable.
     * A real llama.cpp runtime would return true if model is loaded. */
    return false;
}

bool StubPhi4Runtime::IsModelLoaded() const {
    return false;
}

std::string StubPhi4Runtime::Generate(const std::string& prompt, int maxTokens, int timeoutMs) {
    (void)prompt;
    (void)maxTokens;
    (void)timeoutMs;
    return "";
}

std::string StubPhi4Runtime::GetModelPath() const {
    return config_.model_path;
}

void StubPhi4Runtime::Shutdown() {
    initialized_ = false;
}

} /* namespace wsh */
