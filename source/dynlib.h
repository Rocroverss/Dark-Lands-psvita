#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// LLVM/GCC emulated TLS descriptor.
typedef struct __emutls_object {
    size_t size;
    size_t align;
    void *object;  // runtime slot/index (do not mutate from loader on Vita)
    void *templ;   // initializer template
} __emutls_object;

void *__emutls_get_address(void *obj);

#ifdef __cplusplus
}
#endif
