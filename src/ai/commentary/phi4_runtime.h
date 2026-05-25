#pragma once
#ifndef WSH_PHI4_RUNTIME_H
#define WSH_PHI4_RUNTIME_H

#include "phi4_config.h"
#include <string>

namespace wsh {

/* Abstract runtime for Phi-4 model inference.
 * The default stub implementation reports unavailable.
 * A real implementation (e.g., via llama.cpp) can be swapped in. */
class IPhi4Runtime {
public:
    virtual ~IPhi4Runtime() {}

    virtual bool Initialize(const Phi4Config& config) = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool IsModelLoaded() const = 0;
    virtual std::string Generate(const std::string& prompt, int maxTokens, int timeoutMs) = 0;
    virtual std::string GetModelPath() const = 0;
    virtual void Shutdown() = 0;
};

/* Stub runtime — always reports unavailable.
 * Used when WSH_ENABLE_PHI4 is OFF or when llama.cpp is not vendored. */
class StubPhi4Runtime : public IPhi4Runtime {
public:
    StubPhi4Runtime();
    ~StubPhi4Runtime() override;

    bool Initialize(const Phi4Config& config) override;
    bool IsAvailable() const override;
    bool IsModelLoaded() const override;
    std::string Generate(const std::string& prompt, int maxTokens, int timeoutMs) override;
    std::string GetModelPath() const override;
    void Shutdown() override;

private:
    bool initialized_ = false;
    Phi4Config config_;
};

} /* namespace wsh */

#endif /* WSH_PHI4_RUNTIME_H */
