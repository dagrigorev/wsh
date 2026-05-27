#pragma once
#ifndef WSH_SUBPROCESS_PHI4_RUNTIME_H
#define WSH_SUBPROCESS_PHI4_RUNTIME_H

#include "phi4_runtime.h"
#include <windows.h>
#include <string>
#include <mutex>

namespace wsh {

class SubprocessPhi4Runtime : public IPhi4Runtime {
public:
    SubprocessPhi4Runtime();
    ~SubprocessPhi4Runtime() override;

    bool Initialize(const Phi4Config& config) override;
    bool IsAvailable() const override;
    bool IsModelLoaded() const override;
    std::string Generate(const std::string& prompt, int maxTokens, int timeoutMs) override;
    std::string GetModelPath() const override;
    void Shutdown() override;

private:
    std::string FindExe() const;
    bool SpawnProcess();
    void KillProcess();

    bool initialized_ = false;
    bool cli_available_ = false;
    Phi4Config config_;
    std::string exe_path_;

    /* Persistent subprocess handles */
    HANDLE hProcess_ = NULL;
    HANDLE hStdinWrite_ = NULL;   /* We write to this -> process stdin */
    HANDLE hStdoutRead_ = NULL;   /* We read from this <- process stdout */
    std::mutex pipe_mutex_;
};

} /* namespace wsh */

#endif /* WSH_SUBPROCESS_PHI4_RUNTIME_H */
