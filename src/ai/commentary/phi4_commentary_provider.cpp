#include "phi4_commentary_provider.h"
#include "../../core/log.h"
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <cstring>

#ifdef WSH_HAVE_LLAMA
#  include "direct_phi4_runtime.h"
#endif

namespace wsh {

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

Phi4CommentaryProvider::Phi4CommentaryProvider() {}

Phi4CommentaryProvider::~Phi4CommentaryProvider() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        worker_stop_.store(true);
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool Phi4CommentaryProvider::Initialize(const Phi4Config& config, bool fallbackEnabled) {
    /* Guard against double-initialization */
    if (initialized_) return true;

    config_ = config;
    fallback_enabled_ = fallbackEnabled;
    initialized_ = true;

#ifdef WSH_HAVE_LLAMA
    {
        auto direct = std::make_unique<DirectPhi4Runtime>();
        if (direct->Initialize(config_)) {
            /* Runtime accepted the config (model path exists). Model may still
             * be loading on its background thread — the worker will detect when
             * IsAvailable() becomes true and set phi4_available_ accordingly. */
            runtime_ = std::move(direct);
            WSH_LOG_INFO("Phi4CommentaryProvider: direct runtime loading async");
        }
    }
#endif

    if (!runtime_) {
        /* No direct runtime — try subprocess (llama-completion.exe) */
        auto sub = std::make_unique<SubprocessPhi4Runtime>();
        if (sub->Initialize(config_) && sub->IsAvailable()) {
            runtime_ = std::move(sub);
            phi4_available_.store(true);
            WSH_LOG_INFO("Phi4CommentaryProvider: subprocess runtime available");
        } else {
            /* Last resort: stub (always reports unavailable) */
            WSH_LOG_INFO("Phi4CommentaryProvider: no runtime available, using stub");
            runtime_.reset(new StubPhi4Runtime());
            runtime_->Initialize(config_);
            phi4_available_.store(runtime_->IsAvailable());
        }
    }

    if (phi4_available_.load()) {
        WSH_LOG_INFO("Phi-4 runtime available, commentary ready");
    } else if (fallback_enabled_) {
        WSH_LOG_INFO("Phi-4 runtime loading or absent, fallback commentary active");
    } else {
        WSH_LOG_WARN("Phi-4 runtime not available and fallback disabled");
    }

    /* Start worker thread */
    worker_running_.store(true);
    worker_ = std::thread([this]() { WorkerThread(); });

    return true;
}

void Phi4CommentaryProvider::QueueCommentary(const std::string& command) {
    if (!initialized_) return;
    if (command.empty()) return;

    CommentaryRequest req;
    req.command   = command;
    req.timestamp = now_ms();
    req.seq_id    = next_seq_id_++;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (pending_requests_.size() >= 5) {
            pending_requests_.pop();
        }
        pending_requests_.push(req);
    }
    queue_cv_.notify_one();
}

std::string Phi4CommentaryProvider::TryGetCommentary(const std::string& forCommand) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!latest_commentary_.empty() && latest_commentary_command_ == forCommand) {
        std::string result = latest_commentary_;
        latest_commentary_.clear();
        latest_commentary_command_.clear();
        return result;
    }
    return "";
}

std::string Phi4CommentaryProvider::Query(const std::string& prompt) {
    if (prompt.empty()) return "";

    /* If model is still loading, wait up to 15 s for it to become available */
    if (!phi4_available_.load() && runtime_ && runtime_->IsLoading()) {
        WSH_LOG_INFO("Phi-4 model still loading — waiting up to 15s...");
        const int max_wait_ms = 15000;
        int waited = 0;
        while (waited < max_wait_ms && runtime_->IsLoading()) {
            Sleep(100);
            waited += 100;
        }
        if (runtime_->IsAvailable()) {
            phi4_available_.store(true);
            WSH_LOG_INFO("Phi-4 model load completed during query wait");
        }
    }

    if (phi4_available_.load() && runtime_) {
        std::string full_prompt =
            "You are a helpful assistant in a terminal.\n"
            "Answer concisely. Use plain text, not markdown.\n"
            "Maximum 2000 characters.\n\n"
            "User: " + prompt + "\n\nAssistant:\n";
        std::string result = trim(runtime_->Generate(full_prompt, 500, 15000));
        if (!result.empty()) return result;
    }
    return "[ai] \"" + prompt + "\" — phi-4 model not loaded. Build with -DWSH_ENABLE_PHI4=ON or place model.gguf at models/phi-4/";
}

void Phi4CommentaryProvider::InjectResult(const std::string& command, const std::string& text) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    latest_commentary_ = text;
    latest_commentary_command_ = command;
}

bool Phi4CommentaryProvider::IsAvailable() const {
    return initialized_ && (phi4_available_.load() || fallback_enabled_);
}

bool Phi4CommentaryProvider::IsModelLoaded() const {
    return phi4_available_.load() && runtime_ && runtime_->IsModelLoaded();
}

std::string Phi4CommentaryProvider::GetModelPath() const {
    if (runtime_) return runtime_->GetModelPath();
    return config_.model_path;
}

const char* Phi4CommentaryProvider::GetProviderType() const {
    if (phi4_available_.load()) return "phi4";
    if (runtime_ && runtime_->IsLoading()) return "phi4-loading";
    if (fallback_enabled_) return "fallback";
    return "unavailable";
}

void Phi4CommentaryProvider::Clear() {
    std::lock_guard<std::mutex> lock(result_mutex_);
    latest_commentary_.clear();
    latest_commentary_command_.clear();
}

void Phi4CommentaryProvider::SetFallbackEnabled(bool enabled) {
    fallback_enabled_ = enabled;
}

bool Phi4CommentaryProvider::Reload() {
    if (runtime_) {
        runtime_->Shutdown();
    }
    phi4_available_.store(false);

    runtime_.reset(new StubPhi4Runtime());
    if (!runtime_->Initialize(config_)) {
        WSH_LOG_WARN("Phi-4 runtime reload failed");
    }

    phi4_available_.store(runtime_->IsAvailable());

    if (phi4_available_.load()) {
        WSH_LOG_INFO("Phi-4 runtime reloaded successfully");
    } else if (fallback_enabled_) {
        WSH_LOG_INFO("Phi-4 runtime reloaded, using fallback");
    }

    return true;
}

void Phi4CommentaryProvider::WorkerThread() {
    while (true) {
        CommentaryRequest req;
        bool has_work = false;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            /* Wake when work arrives, stop requested, or every 2 s to poll model load */
            queue_cv_.wait_for(lock, std::chrono::seconds(2), [this]() {
                return !pending_requests_.empty() || worker_stop_.load();
            });

            if (worker_stop_.load()) break;

            if (!pending_requests_.empty()) {
                req = pending_requests_.front();
                pending_requests_.pop();
                has_work = true;
            }
        }

        /* Detect when the async model load completes */
        if (!phi4_available_.load() && runtime_ && runtime_->IsAvailable()) {
            phi4_available_.store(true);
            WSH_LOG_INFO("Phi-4 model load complete — commentary now active");
        }

        if (has_work) {
            std::string commentary;

            if (phi4_available_.load() && runtime_) {
                prompt_builder_.SetCommand(req.command);
                prompt_builder_.SetLanguage("ru");
                std::string prompt = prompt_builder_.Build();

                commentary = runtime_->Generate(
                    prompt,
                    config_.max_tokens,
                    config_.commentary_timeout_ms
                );
                commentary = trim(commentary);
            }

            if (commentary.empty() && fallback_enabled_) {
                commentary = fallback_.Generate(req.command);
            }

            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                if (req.seq_id >= latest_seq_id_) {
                    latest_seq_id_ = req.seq_id;
                    latest_commentary_ = commentary;
                    latest_commentary_command_ = req.command;
                }
            }
        }
    }

    worker_running_.store(false);
}

} /* namespace wsh */
