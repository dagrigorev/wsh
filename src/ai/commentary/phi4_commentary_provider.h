#pragma once
#ifndef WSH_PHI4_COMMENTARY_PROVIDER_H
#define WSH_PHI4_COMMENTARY_PROVIDER_H

#include "phi4_config.h"
#include "phi4_runtime.h"
#include "phi4_prompt_builder.h"
#include "fallback_commentary_provider.h"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>

namespace wsh {

/* Structure for a commentary request in the async queue */
struct CommentaryRequest {
    std::string command;
    int64_t timestamp;
    int64_t seq_id;
};

/* Commentary provider that uses Phi-4 model via the runtime abstraction.
 * Manages async inference on a background thread.
 * Falls back to deterministic provider if Phi-4 is unavailable. */
class Phi4CommentaryProvider {
public:
    Phi4CommentaryProvider();
    ~Phi4CommentaryProvider();

    /* Initialize with config. Returns true if Phi-4 or fallback is ready. */
    bool Initialize(const Phi4Config& config, bool fallbackEnabled);

    /* Queue commentary generation for a command. Non-blocking. */
    void QueueCommentary(const std::string& command);

    /* Get the latest ready commentary (non-blocking, may return empty).
     * Pass the command to check staleness. */
    std::string TryGetCommentary(const std::string& forCommand);

    /* Check if commentary is available. */
    bool IsAvailable() const;

    /* Check if Phi-4 model is loaded. */
    bool IsModelLoaded() const;

    /* Get model path for status display. */
    std::string GetModelPath() const;

    /* Get provider type string for status display. */
    const char* GetProviderType() const;

    /* Clear current commentary (stale marker). */
    void Clear();

    /* Toggle fallback on/off */
    void SetFallbackEnabled(bool enabled);

    /* Reload / reinitialize runtime */
    bool Reload();

private:
    void WorkerThread();

    std::unique_ptr<IPhi4Runtime> runtime_;
    Phi4PromptBuilder prompt_builder_;
    FallbackCommentaryProvider fallback_;

    Phi4Config config_;
    bool fallback_enabled_ = true;
    bool initialized_ = false;
    bool phi4_available_ = false;

    /* Async queue state */
    std::mutex queue_mutex_;
    std::mutex result_mutex_;
    std::queue<CommentaryRequest> pending_requests_;
    std::string latest_commentary_;
    std::string latest_commentary_command_;
    int64_t latest_seq_id_ = 0;
    int64_t next_seq_id_ = 0;
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> worker_stop_{false};
    std::thread worker_;

    /* Config from toml */
    std::string model_path_;
};

} /* namespace wsh */

#endif /* WSH_PHI4_COMMENTARY_PROVIDER_H */
