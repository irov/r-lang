#include "r_library_sync_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Noreturn void r_library_internal_sync_contract_violation(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

uintptr_t r_library_internal_sync_current_thread_token(void) {
    pthread_t thread = pthread_self();
    uintptr_t token = (uintptr_t)0U;

    _Static_assert(sizeof(thread) <= sizeof(token), "pthread_t must fit in the sync owner token");
    (void)memcpy(&token, &thread, sizeof(thread));
    return token;
}

void r_library_internal_sync_move_initialize(RRuntimeTypeInfo type,
                                             void *destination,
                                             void *source) {
    if ((type.size == 0U) || (destination == source)) {
        return;
    }
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}
