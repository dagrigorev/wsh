#include <cstdio>
#include <cstdlib>
#include <windows.h>

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("1. Testing VEH for STATUS_STACK_BUFFER_OVERRUN...\n");

    int vind = 0;
    PVOID veh = AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS ei) -> LONG {
        if (ei->ExceptionRecord->ExceptionCode == 0xC0000409) {
            printf("   VEH caught 0xC0000409!\n");
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (ei->ExceptionRecord->ExceptionCode == STATUS_FATAL_APP_EXIT) {
            printf("   VEH caught STATUS_FATAL_APP_EXIT!\n");
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        printf("   VEH: unhandled code=0x%08lX\n", ei->ExceptionRecord->ExceptionCode);
        return EXCEPTION_CONTINUE_SEARCH;
    });

    printf("2. Raising STATUS_STACK_BUFFER_OVERRUN via RaiseException...\n");
    RaiseException(0xC0000409, 0, 0, NULL);
    printf("3. After RaiseException\n");

    printf("4. Calling abort()...\n");
    __try {
        abort();
        printf("5. After abort (no crash)\n");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("5. __except caught code=0x%08lX\n", GetExceptionCode());
    }

    printf("6. Done\n");
    RemoveVectoredExceptionHandler(veh);
    return 0;
}
