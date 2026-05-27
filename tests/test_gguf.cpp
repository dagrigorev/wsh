#include <gguf.h>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    const char* model_path = argc > 1 ? argv[1] : "models/phi-4/model.gguf";
    printf("Model: %s\n", model_path);

    printf("1. Opening GGUF file directly...\n");
    fflush(stdout);

    struct gguf_init_params params = {
        /*.no_alloc   =*/ false,
        /*.ctx        =*/ NULL,
    };

    __try {
        struct gguf_context* ctx = gguf_init_from_file(model_path, params);
        if (ctx) {
            printf("2. GGUF opened! n_tensors=%d n_kv=%d\n",
                   gguf_get_n_tensors(ctx), gguf_get_n_kv(ctx));
            gguf_free(ctx);
        } else {
            printf("2. gguf_init_from_file returned NULL\n");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("2. CRASH (code=0x%08lX)\n", GetExceptionCode());
    }

    printf("3. Done\n");
    return 0;
}
