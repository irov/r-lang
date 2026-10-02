#ifndef R_RUNTIME_TYPE_H
#define R_RUNTIME_TYPE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RRuntimeMoveInitializeFn)(void *destination, void *source);
typedef void (*RRuntimeDropFn)(void *value);

typedef struct RRuntimeTypeInfo {
    size_t size;
    size_t alignment;
    RRuntimeMoveInitializeFn move_initialize;
    RRuntimeDropFn drop;
} RRuntimeTypeInfo;

#ifdef __cplusplus
}
#endif

#endif
