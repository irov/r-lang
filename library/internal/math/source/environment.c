#include "r_library_math_environment.h"

#include "r_runtime_0_1.h"

#include <errno.h>
#include <fenv.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

static _Noreturn void r_library_internal_math_environment_native_failure(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

void r_library_internal_math_environment_begin(RLibraryMathEnvironment *environment) {
    fenv_t held_environment;

    environment->caller_errno = errno;
    if (fegetenv(&environment->caller_environment) != 0) {
        errno = environment->caller_errno;
        r_library_internal_math_environment_native_failure();
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&environment->caller_environment);
        errno = environment->caller_errno;
        r_library_internal_math_environment_native_failure();
    }
    errno = 0;
    environment->active = 1;
}

RLibraryMathIndicators
r_library_internal_math_environment_end(RLibraryMathEnvironment *environment) {
    RLibraryMathIndicators indicators;

    indicators.native_errno = errno;
    indicators.floating_exceptions = fetestexcept(FE_ALL_EXCEPT);
    environment->active = 0;
    if (fesetenv(&environment->caller_environment) != 0) {
        errno = environment->caller_errno;
        r_library_internal_math_environment_native_failure();
    }
    errno = environment->caller_errno;
    return indicators;
}
