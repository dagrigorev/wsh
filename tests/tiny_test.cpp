#include <cstdio>
#include <windows.h>
int main() {
    printf("step1\n");
    HMODULE h = LoadLibraryA("llama.dll");
    printf("step2: h=%p\n", (void*)h);
    if (h) {
        FreeLibrary(h);
    }
    printf("done\n");
    return 0;
}
