#include <llama.h>
#include <ggml.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>

static void model_load_abort_callback(const char* msg) {
    (void)msg;
}

static std::string generate_once(llama_model* model, llama_context* ctx, const std::string& prompt, int max_tokens, float temperature) {
    const llama_vocab* vocab = llama_model_get_vocab(model);
    int n_vocab = llama_vocab_n_tokens(vocab);

    std::string formatted = "<|im_start|>user<|im_sep|>" + prompt + "<|im_end|>\n<|im_start|>assistant<|im_sep|>";

    int n_tokens = (int)formatted.size() + 4;
    std::vector<llama_token> tokens(n_tokens);
    n_tokens = llama_tokenize(vocab, formatted.data(), (int)formatted.size(), tokens.data(), n_tokens, true, false);
    if (n_tokens <= 0) return "";
    tokens.resize(n_tokens);

    std::vector<llama_token> output;

    for (int i = 0; i < max_tokens; ++i) {
        llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
        if (llama_decode(ctx, batch)) break;

        float* logits = llama_get_logits(ctx);
        if (!logits) break;

        llama_token next_token = 0;

        if (temperature <= 0.0f) {
            float max_val = logits[0];
            next_token = 0;
            for (int j = 1; j < n_vocab; ++j) {
                if (logits[j] > max_val) {
                    max_val = logits[j];
                    next_token = (llama_token)j;
                }
            }
        } else {
            float inv_temp = 1.0f / temperature;
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

        if (next_token == llama_token_eos(vocab)) break;

        output.push_back(next_token);

        tokens.resize(1);
        tokens[0] = next_token;
    }

    std::string result;
    for (llama_token t : output) {
        char buf[16] = {0};
        int n = llama_token_to_piece(vocab, t, buf, sizeof(buf), 0, false);
        if (n > 0) result.append(buf, n);
    }
    return result;
}

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    const char* model_path = argc > 1 ? argv[1] : "models/phi-4/model.gguf";
    printf("Model: %s\n", model_path);

    ggml_abort_callback_t old_cb = ggml_set_abort_callback(model_load_abort_callback);

    llama_backend_init();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;
    mparams.use_mmap = false;

    printf("Loading model...\n");
    llama_model* model = llama_load_model_from_file(model_path, mparams);
    if (!model) {
        printf("FAILED to load model\n");
        ggml_set_abort_callback(old_cb);
        return 1;
    }
    printf("Model loaded!\n");

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 2048;
    cparams.n_batch = 512;
    llama_context* ctx = llama_new_context_with_model(model, cparams);
    if (!ctx) {
        printf("FAILED to create context\n");
        llama_free_model(model);
        ggml_set_abort_callback(old_cb);
        return 1;
    }
    printf("Context created!\n");

    std::srand((unsigned)std::time(nullptr));

    const char* prompt = "Hey";
    printf("\nPrompt: \"%s\"\n", prompt);
    printf("Generating...\n");

    std::string result = generate_once(model, ctx, prompt, 50, 0.0f);
    printf("Response: \"%s\"\n", result.c_str());

    llama_free(ctx);
    llama_free_model(model);
    llama_backend_free();
    ggml_set_abort_callback(old_cb);
    printf("\nDone\n");
    return 0;
}
