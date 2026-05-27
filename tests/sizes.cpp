#include <llama.h>
#include <cstdio>
#pragma comment(lib, "delayimp.lib")
#pragma comment(linker, "/DELAYLOAD:llama.dll")
int main() {
    printf("sizeof llama_model_params=%zu\n", sizeof(llama_model_params));
    printf("sizeof llama_context_params=%zu\n", sizeof(llama_context_params));
    printf("sizeof llama_batch=%zu\n", sizeof(llama_batch));
    printf("sizeof llama_token=%zu\n", sizeof(llama_token));
    llama_backend_init();
    printf("backend init OK\n");
    llama_model_params p = llama_model_default_params();
    printf("default params OK, n_gpu_layers=%d\n", p.n_gpu_layers);
    return 0;
}
