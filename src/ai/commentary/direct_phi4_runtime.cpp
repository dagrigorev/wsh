#include "direct_phi4_runtime.h"
#include "../../core/log.h"
#include <llama.h>
#include <ggml.h>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <setjmp.h>

namespace wsh {

/* Jump buffer used to survive ggml_abort() during model loading */
static thread_local jmp_buf g_abort_jmp;

static void model_load_abort_callback(const char* msg) {
    (void)msg;
    longjmp(g_abort_jmp, 1);
}

DirectPhi4Runtime::DirectPhi4Runtime() {}
DirectPhi4Runtime::~DirectPhi4Runtime() { Shutdown(); }

bool DirectPhi4Runtime::Initialize(const Phi4Config& config) {
    config_ = config;
    initialized_ = true;

    if (config_.model_path.empty()) {
        WSH_LOG_WARN("DirectPhi4Runtime: no model path configured");
        return true;
    }

    DWORD attrs = GetFileAttributesA(config_.model_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        WSH_LOG_WARN("DirectPhi4Runtime: model not found at %s", config_.model_path.c_str());
        return true;
    }

    WSH_LOG_INFO("DirectPhi4Runtime: loading model from %s", config_.model_path.c_str());

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;
    mparams.use_mmap = false;

    /* Install ggml abort callback to survive GGML_ASSERT/GGML_ABORT crashes */
    ggml_abort_callback_t old_cb = ggml_set_abort_callback(model_load_abort_callback);
    bool aborted = false;

    if (setjmp(g_abort_jmp) == 0) {
        model_ = llama_load_model_from_file(config_.model_path.c_str(), mparams);
    } else {
        WSH_LOG_WARN("DirectPhi4Runtime: model loading aborted (GGML_ASSERT), falling back");
        aborted = true;
        model_ = nullptr;
    }

    ggml_set_abort_callback(old_cb);
    if (aborted) {
        llama_backend_free();
        return true;
    }
    if (!model_) {
        WSH_LOG_WARN("DirectPhi4Runtime: failed to load model");
        llama_backend_free();
        return true;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = (uint32_t)config_.context_tokens;
    cparams.n_batch = 512;
    ctx_ = llama_new_context_with_model((llama_model*)model_, cparams);
    if (!ctx_) {
        WSH_LOG_WARN("DirectPhi4Runtime: failed to create context");
        llama_free_model((llama_model*)model_);
        model_ = nullptr;
        llama_backend_free();
        return true;
    }

    model_available_ = true;
    WSH_LOG_INFO("DirectPhi4Runtime: model loaded successfully");
    return true;
}

std::string DirectPhi4Runtime::GenerateInternal(const std::string& prompt, int maxTokens) {
    llama_model* model = (llama_model*)model_;
    llama_context* ctx = (llama_context*)ctx_;
    if (!model || !ctx) return "";

    /* Format prompt using phi-4 chat template */
    std::string formatted = "<|im_start|>user<|im_sep|>" + prompt + "<|im_end|>\n<|im_start|>assistant<|im_sep|>";

    const llama_vocab* vocab = llama_model_get_vocab(model);
    int n_vocab = llama_vocab_n_tokens(vocab);

    /* Tokenize */
    int n_tokens = formatted.size() + 4;
    std::vector<llama_token> tokens(n_tokens);
    n_tokens = llama_tokenize(vocab, formatted.data(), (int)formatted.size(),
                              tokens.data(), n_tokens, true, false);
    if (n_tokens <= 0) return "";
    tokens.resize(n_tokens);

    /* Generation loop */
    std::vector<llama_token> output;

    for (int i = 0; i < maxTokens; ++i) {
        llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
        if (llama_decode(ctx, batch)) break;

        /* Read logits for last token */
        float* logits = llama_get_logits(ctx);
        if (!logits) break;

        /* Apply temperature and sample */
        float temp = config_.temperature;
        llama_token next_token = 0;

        if (temp <= 0.0f) {
            /* Greedy */
            float max_val = logits[0];
            next_token = 0;
            for (int j = 1; j < n_vocab; ++j) {
                if (logits[j] > max_val) {
                    max_val = logits[j];
                    next_token = (llama_token)j;
                }
            }
        } else {
            /* Temperature-scaled softmax sampling */
            float inv_temp = 1.0f / temp;
            float max_logit = logits[0];
            for (int j = 1; j < n_vocab; ++j) {
                if (logits[j] > max_logit) max_logit = logits[j];
            }
            std::vector<double> probs(n_vocab);
            double sum = 0.0;
            for (int j = 0; j < n_vocab; ++j) {
                double p = std::exp((double)(logits[j] - max_logit) * inv_temp);
                probs[j] = p;
                sum += p;
            }
            double r = (double)std::rand() / (double)RAND_MAX * sum;
            double cum = 0.0;
            for (int j = 0; j < n_vocab; ++j) {
                cum += probs[j];
                if (cum >= r) {
                    next_token = (llama_token)j;
                    break;
                }
            }
        }

        /* Check EOS */
        if (next_token == llama_token_eos(vocab)) break;

        output.push_back(next_token);

        /* Prepare next input: single token */
        tokens.resize(1);
        tokens[0] = next_token;

        /* Check context size */
        if ((int)output.size() >= maxTokens) break;
    }

    /* Detokenize */
    std::string result;
    for (llama_token t : output) {
        char buf[16] = {0};
        int n = llama_token_to_piece(vocab, t, buf, sizeof(buf), 0, false);
        if (n > 0) result.append(buf, n);
    }

    return result;
}

std::string DirectPhi4Runtime::Generate(const std::string& prompt, int maxTokens, int timeoutMs) {
    if (!model_available_ || prompt.empty()) return "";
    std::lock_guard<std::mutex> lock(gen_mutex_);
    return GenerateInternal(prompt, maxTokens);
}

bool DirectPhi4Runtime::IsAvailable() const {
    return initialized_ && model_available_;
}

bool DirectPhi4Runtime::IsModelLoaded() const {
    return model_available_;
}

std::string DirectPhi4Runtime::GetModelPath() const {
    return config_.model_path;
}

void DirectPhi4Runtime::Shutdown() {
    if (ctx_) {
        llama_free((llama_context*)ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_free_model((llama_model*)model_);
        model_ = nullptr;
    }
    llama_backend_free();
    model_available_ = false;
    initialized_ = false;
}

} /* namespace wsh */
