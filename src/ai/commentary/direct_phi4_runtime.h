#pragma once
#ifndef WSH_DIRECT_PHI4_RUNTIME_H
#define WSH_DIRECT_PHI4_RUNTIME_H

#include "phi4_runtime.h"
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

namespace wsh {

class DirectPhi4Runtime : public IPhi4Runtime {
public:
    DirectPhi4Runtime();
    ~DirectPhi4Runtime() override;

    /* Initialize validates the model path and starts async loading.
     * Returns true if model path exists (loading may still be in progress). */
    bool Initialize(const Phi4Config& config) override;
    bool IsAvailable() const override;
    /* True while the background loading thread is still running. */
    bool IsLoading() const override;
    bool IsModelLoaded() const override;
    std::string Generate(const std::string& prompt, int maxTokens, int timeoutMs) override;
    std::string GetModelPath() const override;
    void Shutdown() override;

private:
    std::string GenerateInternal(const std::string& prompt, int maxTokens);
    void LoadModel();   /* background loading thread entry point */

    bool initialized_ = false;
    std::atomic<bool> model_available_{false};
    std::atomic<bool> loading_{false};
    Phi4Config config_;

    void* model_ = nullptr;   /* llama_model* */
    void* ctx_ = nullptr;     /* llama_context* */
    std::mutex gen_mutex_;
    std::thread load_thread_;
};

} /* namespace wsh */

#endif /* WSH_DIRECT_PHI4_RUNTIME_H */
