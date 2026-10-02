#ifndef R_LIBRARY_MATH_ENVIRONMENT_H
#define R_LIBRARY_MATH_ENVIRONMENT_H

#include <fenv.h>

typedef struct RLibraryMathEnvironment {
    fenv_t caller_environment;
    int caller_errno;
    _Bool active;
} RLibraryMathEnvironment;

typedef struct RLibraryMathIndicators {
    int native_errno;
    int floating_exceptions;
} RLibraryMathIndicators;

void r_library_internal_math_environment_begin(RLibraryMathEnvironment *environment);
RLibraryMathIndicators
r_library_internal_math_environment_end(RLibraryMathEnvironment *environment);

#endif
