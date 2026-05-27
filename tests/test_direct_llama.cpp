#include <llama.h>
#include <ggml.h>
#include <cstdio>
#include <cstdlib>

static void model_load_abort_callback(const char* msg) {
    (void)msg;
}

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    const char* model_path = argc > 1 ? argv[1] : "models/phi-4/model.gguf";
    printf("Model: %s\n", model_path);

    ggml_abort_callback_t old_cb = ggml_set_abort_callback(model_load_abort_callback);

    llama_backend_init();
    printf("1. backend init\n");

    // Step 1: vocab_only = true (metadata + tokenizer, no tensors)
    llama_model_params mparams = llama_model_default_params();
    mparams.vocab_only = true;
    mparams.n_gpu_layers = 0;
    mparams.use_mmap = false;
    printf("2. vocab_only=true\n");

    llama_model* model = llama_load_model_from_file(model_path, mparams);
    if (model) {
        printf("3. Model loaded (vocab)!\n");
        const llama_vocab* vocab = llama_model_get_vocab(model);
        printf("   Vocab: %d tokens\n", llama_vocab_n_tokens(vocab));
        llama_free_model(model);
    } else {
        printf("3. Model returned NULL\n");
    }

    // Step 2: full model load (vocab + tensors)
    printf("\n4. Full model load...\n");
    mparams.vocab_only = false;

    llama_model* model2 = llama_load_model_from_file(model_path, mparams);
    if (model2) {
        printf("5. Full model loaded!\n");
        const llama_vocab* vocab = llama_model_get_vocab(model2);
        printf("   Vocab: %d tokens\n", llama_vocab_n_tokens(vocab));
        llama_free_model(model2);
    } else {
        printf("5. NULL (failed)\n");
    }

    llama_backend_free();
    ggml_set_abort_callback(old_cb);
    printf("\nDone\n");
    return 0;
}
