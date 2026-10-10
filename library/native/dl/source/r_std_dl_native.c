#include "r_std_dl_native.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <string.h>

/* The longest path or symbol name passed to the loader, as PATH_MAX of the target. */
#define R_STD_DL_NATIVE_TEXT_CAPACITY 1025U

/* The bytes of an R string as a C string in `terminated`; false for an empty text, one that
   holds a NUL byte or one longer than the capacity. */
static bool r_std_dl_native_c_string(const uint8_t *bytes,
                                     size_t length,
                                     char terminated[R_STD_DL_NATIVE_TEXT_CAPACITY]) {
    if ((bytes == NULL) || (length == 0U) || (length >= R_STD_DL_NATIVE_TEXT_CAPACITY) ||
        (memchr(bytes, 0, length) != NULL)) {
        return false;
    }
    (void)memcpy(terminated, bytes, length);
    terminated[length] = '\0';
    return true;
}

int32_t r_std_dl_native_open(const uint8_t *path, size_t length, uint64_t *handle) {
    char terminated[R_STD_DL_NATIVE_TEXT_CAPACITY];
    void *library;

    if ((path == NULL) || (length == 0U) || (memchr(path, 0, length) != NULL)) {
        return -1;
    }
    if (!r_std_dl_native_c_string(path, length, terminated)) {
        /* Longer than any path the loader opens. */
        return 0;
    }
    /* R-SLIB-DL-0002: bound at opening, local to the library; never closed, so every address
       obtained from it stays valid. */
    library = dlopen(terminated, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        return 0;
    }
    /* The token is the address of the loader's handle: the library is never closed. */
    *handle = (uint64_t)(uintptr_t)library;
    return 1;
}

void *r_std_dl_native_symbol(uint64_t handle, const uint8_t *name, size_t length) {
    char terminated[R_STD_DL_NATIVE_TEXT_CAPACITY];

    if ((handle == 0U) || !r_std_dl_native_c_string(name, length, terminated)) {
        return NULL;
    }
    return dlsym((void *)(uintptr_t)handle, terminated);
}
