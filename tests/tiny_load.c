#include <stdio.h>
#include <windows.h>

/* Dynamic load llama.dll to avoid import library issues */
typedef void (*llama_backend_init_t)(void);
typedef void (*llama_backend_free_t)(void);
typedef struct llama_model* (*llama_load_model_t)(const char*, struct llama_model_params);
typedef void (*llama_free_model_t)(struct llama_model*);

/* We need to define the struct manually since we're not including llama.h */
struct llama_model_params {
    void** devices;
    const void* tensor_buft_overrides;
    int32_t n_gpu_layers;
    int split_mode;
    int32_t main_gpu;
    const float* tensor_split;
    void* progress_callback;
    void* progress_callback_user_data;
    const void* kv_overrides;
    int vocab_only;
    int use_mmap;
    int use_direct_io;
    int use_mlock;
    int check_tensors;
    int use_extra_bufts;
    int no_host;
    int no_alloc;
};

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    
    if (argc < 2) {
        printf("Usage: %s <model.gguf>\n", argv[0]);
        return 1;
    }
    
    printf("Loading llama.dll...\n");
    HMODULE h = LoadLibraryA("llama.dll");
    if (!h) { printf("FAIL: LoadLibrary error=%lu\n", GetLastError()); return 1; }
    
    llama_backend_init_t fn_backend_init = (llama_backend_init_t)GetProcAddress(h, "llama_backend_init");
    llama_backend_free_t fn_backend_free = (llama_backend_free_t)GetProcAddress(h, "llama_backend_free");
    llama_load_model_t fn_load = (llama_load_model_t)GetProcAddress(h, "llama_load_model_from_file");
    llama_free_model_t fn_free_model = (llama_free_model_t)GetProcAddress(h, "llama_free_model");
    
    if (!fn_backend_init || !fn_backend_free || !fn_load || !fn_free_model) {
        printf("FAIL: GetProcAddress error\n"); return 1;
    }
    printf("All functions loaded\n");
    
    fn_backend_init();
    printf("Backend initialized\n");
    
    struct llama_model_params params = {0};
    params.n_gpu_layers = 0;
    params.use_mmap = 1;
    params.use_mlock = 0;
    params.vocab_only = 0;
    params.no_alloc = 0;
    
    printf("Loading model: %s\n", argv[1]);
    struct llama_model* model = fn_load(argv[1], params);
    if (!model) { printf("FAIL: model load returned NULL\n"); fn_backend_free(); return 1; }
    printf("Model loaded! model=%p\n", (void*)model);
    
    fn_free_model(model);
    fn_backend_free();
    printf("Done\n");
    return 0;
}
