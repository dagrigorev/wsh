# MSVC input.c uint32_t fix

Build log showed `src/platform/input.c` failing with `uint32_t: undeclared identifier`.

`input.c` uses UTF-16 surrogate-pair handling and stores the decoded Unicode scalar value in `uint32_t`, but the translation unit did not include `<stdint.h>` directly. Some other files received `uint32_t` indirectly through local headers, but `input.c` must not rely on include order.

Fix:

```c
#include <stdint.h>
```

was added to `src/platform/input.c`.
