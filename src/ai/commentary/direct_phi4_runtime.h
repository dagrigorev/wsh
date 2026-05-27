#pragma once
#ifndef WSH_DIRECT_PHI4_RUNTIME_H
#define WSH_DIRECT_PHI4_RUNTIME_H

#include "phi4_runtime.h"
#include <string>
#include <mutex>

namespace wsh {

class DirectPhi4Runtime : public IPhi4Runtime {
public:
    DirectPhi4Runtime();
    ~DirectPhi4Runtime() override;

    bool Initialize(const Phi4Config& config) override;
    bool IsAvailable() const override;
    bool IsModelLoaded() const override;
    std::string Generate(const std::string& prompt, int maxTokens, int timeoutMs) override;
    std::string GetModelPath() const override;
    void Shutdown() override;

private:
    std::string GenerateInternal(const std::string& prompt, int maxTokens);

    bool initialized_ = false;
    bool model_available_ = false;
    Phi4Config config_;

    void* model_ = nullptr;   /* llama_model* */
    void* ctx_ = nullptr;     /* llama_context* */
    std::mutex gen_mutex_;
};

} /* namespace wsh */

#endif /* WSH_DIRECT_PHI4_RUNTIME_H */
