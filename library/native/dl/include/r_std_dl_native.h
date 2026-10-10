#ifndef R_STD_DL_NATIVE_H
#define R_STD_DL_NATIVE_H

/* The native provider of std.dl (Library R-SLIB-DL-0001..0004): shared libraries the program
   opens while it runs, through the dynamic loader of the target (Core R-FFI-0063). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the shared library at the `length` bytes of `path` with every symbol bound now and none
   made visible to other libraries. 1 with *handle set to a nonzero token of the library; 0 when
   the loader refuses the library; -1 for an empty path or one that holds a NUL byte. A library
   stays open until the program ends, so its token stays valid on every thread. */
int32_t r_std_dl_native_open(const uint8_t *path, size_t length, uint64_t *handle);

/* The address of the symbol named by the `length` bytes of `name` in the library of the token,
   or NULL when the library defines none or the name is empty or holds a NUL byte. */
void *r_std_dl_native_symbol(uint64_t handle, const uint8_t *name, size_t length);

#ifdef __cplusplus
}
#endif

#endif
