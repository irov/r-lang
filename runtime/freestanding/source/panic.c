#include "r_runtime_freestanding.h"

_Noreturn void r_runtime_panic(RRuntimePanicCategory category, RRuntimeSourceSpan span) {
    r_runtime_environment_panic(category, span);
}

const char *r_runtime_panic_category_name(RRuntimePanicCategory category) {
    switch (category) {
    case R_RUNTIME_PANIC_EXPLICIT:
        return "explicit";
    case R_RUNTIME_PANIC_BOUNDS:
        return "bounds";
    case R_RUNTIME_PANIC_INTEGER_OVERFLOW:
        return "integer_overflow";
    case R_RUNTIME_PANIC_DIVISION_BY_ZERO:
        return "division_by_zero";
    case R_RUNTIME_PANIC_INVALID_SHIFT:
        return "invalid_shift";
    case R_RUNTIME_PANIC_INVALID_CONVERSION:
        return "invalid_conversion";
    case R_RUNTIME_PANIC_ALLOCATION_FAILURE:
        return "allocation_failure";
    case R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW:
        return "reference_count_overflow";
    case R_RUNTIME_PANIC_SCOPED_THREAD_PANIC:
        return "scoped_thread_panic";
    case R_RUNTIME_PANIC_ONCE_POISONED:
        return "once_poisoned";
    case R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME:
        return "thread_local_lifetime";
    case R_RUNTIME_PANIC_STACK_EXHAUSTION:
        return "stack_exhaustion";
    case R_RUNTIME_PANIC_CONTRACT_VIOLATION:
        return "contract_violation";
    default:
        return "contract_violation";
    }
}
