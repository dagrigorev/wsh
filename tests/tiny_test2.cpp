#include <cstdio>
#include <windows.h>

typedef void (*llama_backend_init_t)();
typedef struct llama_model_params (*llama_model_default_params_t)();
typedef struct llama_model* (*llama_load_model_from_file_t)(const char*, struct llama_model_params);

int main() {
    printf("step1: loading llama.dll...\n");
    HMODULE h = LoadLibraryA("llama.dll");
    if (!h) { printf("FAIL: LoadLibrary error=%lu\n", GetLastError()); return 1; }
    printf("step2: loaded at %p\n", (void*)h);

    llama_backend_init_t fn_init = (llama_backend_init_t)GetProcAddress(h, "llama_backend_init");
    llama_model_default_params_t fn_params = (llama_model_default_params_t)GetProcAddress(h, "llama_model_default_params");
    llama_load_model_from_file_t fn_load = (llama_load_model_from_file_t)GetProcAddress(h, "llama_load_model_from_file");
    printf("step3: got procs init=%p params=%p load=%p\n", (void*)fn_init, (void*)fn_params, (void*)fn_load);

    fn_init();
    printf("step4: backend init OK\n");

    struct llama_model_params mparams = fn_params();
    printf("step5: got default params\n");

    struct llama_model* model = fn_load("D:\\sources\\repos\\personal\\wsh\\build\\dist\\models\\phi-4\\model.gguf", mparams);
    printf("step6: model=%p\n", (void*)model);

    FreeLibrary(h);
    printf("done\n");
    return 0;
}
