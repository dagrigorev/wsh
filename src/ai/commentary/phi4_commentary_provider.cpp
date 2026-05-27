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
    config_ = config;
    fallback_enabled_ = fallbackEnabled;
    initialized_ = true;

    /* Try direct in-process runtime first (requires vendored llama.cpp) */
#ifdef WSH_HAVE_LLAMA
    {
        auto direct = std::make_unique<DirectPhi4Runtime>();
        if (direct->Initialize(config_) && direct->IsAvailable()) {
            runtime_ = std::move(direct);
            phi4_available_ = true;
            WSH_LOG_INFO("Direct in-process Phi-4 runtime available");
        }
    }
#endif
    if (!phi4_available_) {
        /* Fall back to subprocess runtime (llama-completion.exe) */
        auto sub = std::make_unique<SubprocessPhi4Runtime>();
        if (sub->Initialize(config_) && sub->IsAvailable()) {
            runtime_ = std::move(sub);
            phi4_available_ = true;
            WSH_LOG_INFO("Subprocess Phi-4 runtime available (llama-completion.exe)");
        } else {
            /* Fall back to stub */
            WSH_LOG_INFO("Phi-4 runtime unavailable, trying stub");
            runtime_.reset(new StubPhi4Runtime());
            if (!runtime_->Initialize(config_)) {
                WSH_LOG_WARN("Phi-4 runtime initialization failed");
            }
            phi4_available_ = runtime_->IsAvailable();
        }
    }

    if (phi4_available_) {
        WSH_LOG_INFO("Phi-4 runtime available, commentary ready");
    } else if (fallback_enabled_) {
        WSH_LOG_INFO("Phi-4 runtime not available, using fallback commentary");
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
    req.command = command;
    req.timestamp = now_ms();
    req.seq_id = next_seq_id_++;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        /* Keep queue small — discard old requests */
        while (pending_requests_.size() >= 5) {
            pending_requests_.pop();
        }
        pending_requests_.push(req);
    }
    /* Wake the worker immediately instead of waiting for the next poll tick */
    queue_cv_.notify_one();
}

std::string Phi4CommentaryProvider::TryGetCommentary(const std::string& forCommand) {
    /* Check if Phi-4/fallback has a result ready */
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
    if (phi4_available_ && runtime_) {
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
    return initialized_ && (phi4_available_ || fallback_enabled_);
}

bool Phi4CommentaryProvider::IsModelLoaded() const {
    return phi4_available_ && runtime_ && runtime_->IsModelLoaded();
}

std::string Phi4CommentaryProvider::GetModelPath() const {
    if (runtime_) return runtime_->GetModelPath();
    return config_.model_path;
}

const char* Phi4CommentaryProvider::GetProviderType() const {
    if (phi4_available_) return "phi4";
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
    phi4_available_ = false;

    runtime_.reset(new StubPhi4Runtime());
    if (!runtime_->Initialize(config_)) {
        WSH_LOG_WARN("Phi-4 runtime reload failed");
    }

    phi4_available_ = runtime_->IsAvailable();

    if (phi4_available_) {
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
            /* Wait until there's work or we're asked to stop.
             * condition_variable replaces the old Sleep(50) polling loop,
             * so commentary is generated the moment a command is queued. */
            queue_cv_.wait(lock, [this]() {
                return !pending_requests_.empty() || worker_stop_.load();
            });

            if (worker_stop_.load()) break;

            if (!pending_requests_.empty()) {
                req = pending_requests_.front();
                pending_requests_.pop();
                has_work = true;
            }
        }

        if (has_work) {
            std::string commentary;

            /* Try Phi-4 first */
            if (phi4_available_ && runtime_) {
                prompt_builder_.SetCommand(req.command);
                prompt_builder_.SetLanguage("ru");
                std::string prompt = prompt_builder_.Build();

                commentary = runtime_->Generate(
                    prompt,
                    config_.max_tokens,
                    250 /* timeout */
                );

                /* Trim response */
                commentary = trim(commentary);
            }

            /* Fall back to deterministic if Phi-4 didn't produce anything */
            if (commentary.empty() && fallback_enabled_) {
                commentary = fallback_.Generate(req.command);
            }

            /* Cache result */
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
