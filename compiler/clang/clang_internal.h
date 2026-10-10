#ifndef R_CLANG_INTERNAL_H
#define R_CLANG_INTERNAL_H

#include "frontend_internal.h"
#include "r_frontend.h"

#include <clang-c/Index.h>

#include <stdbool.h>
#include <stddef.h>

/* The name of the translation unit libclang parses from memory. */
#define R_CLANG_UNIT_NAME "r-verify.c"

/* R-FFI-0040: the libclang is the pinned C compiler release and sysroot the pinned SDK. */
bool r_clang_check_toolchain(RFrontendContext *context, const RFrontendHeaderOptions *options);

/* The command line of a parse: the generated-C contract options, the target, the SDK, the
   resource directory of the linked libclang and the header roots. The array is allocated from
   the context and released with r_context_free. */
const char **
r_clang_arguments(RFrontendContext *context, const RFrontendHeaderOptions *options, size_t *count);

/* Parses source as the unit R_CLANG_UNIT_NAME with the CXTranslationUnit flags;
   R_FRONTEND_INTERNAL_ERROR when libclang fails. */
RFrontendStatus r_clang_parse(RFrontendContext *context,
                              const RFrontendHeaderOptions *options,
                              const char *source,
                              size_t length,
                              unsigned flags,
                              CXIndex *index,
                              CXTranslationUnit *unit);

/* Appends text to the header report of the context. */
bool r_clang_report_append(RFrontendContext *context, const char *text, size_t length);

/* Appends the error diagnostics of a unit to the header report; *failed when there is one. */
bool r_clang_report_errors(RFrontendContext *context, CXTranslationUnit unit, bool *failed);

#endif
