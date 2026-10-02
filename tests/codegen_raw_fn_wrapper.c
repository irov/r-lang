#include "r_runtime_0_1.h"

#include <fenv.h>
#include <pthread.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

extern int r_audit_increment(int value);
extern double r_audit_rounding(double left, double right);
extern double r_audit_invoke(double (*callback)(double, double));
extern int (*r_audit_select(_Bool doubled))(int);

static _Thread_local int r_test_native_entry_status;

static double r_test_native_callback(double left, double right) {
    const double result = left + right;

    if (fegetround() != FE_TONEAREST || fetestexcept(FE_ALL_EXCEPT) != 0 ||
        r_audit_increment(41) != 42) {
        r_test_native_entry_status = 107;
    }
    if (fesetround(FE_UPWARD) != 0 || feraiseexcept(FE_INVALID) != 0) {
        r_test_native_entry_status = 108;
    }
    return result;
}

static int r_test_foreign_callback(void) {
    fenv_t saved;
    int status = 0;

    if (fegetenv(&saved) != 0 || fesetround(FE_UPWARD) != 0 || feclearexcept(FE_ALL_EXCEPT) != 0 ||
        feraiseexcept(FE_DIVBYZERO) != 0) {
        return 101;
    }
    if (r_audit_increment(41) != 42 || fegetround() != FE_UPWARD ||
        fetestexcept(FE_ALL_EXCEPT) != FE_DIVBYZERO) {
        status = 102;
    }
    /* A halfway sum rounds to even inside R, despite the foreign caller's mode. */
    if (r_audit_rounding(1.0, 0x1p-53) != 1.0 || fegetround() != FE_UPWARD ||
        fetestexcept(FE_ALL_EXCEPT) != FE_DIVBYZERO) {
        status = 103;
    }
    r_test_native_entry_status = 0;
    if (r_audit_invoke(r_test_native_callback) != 1.0 || r_test_native_entry_status != 0 ||
        fegetround() != FE_UPWARD || fetestexcept(FE_ALL_EXCEPT) != FE_DIVBYZERO) {
        status = 109;
    }
    {
        int (*increment)(int) = r_audit_select(0);
        int (*double_value)(int) = r_audit_select(1);
        if (increment == NULL || double_value == NULL || increment(41) != 42 ||
            double_value(21) != 42 || fegetround() != FE_UPWARD ||
            fetestexcept(FE_ALL_EXCEPT) != FE_DIVBYZERO) {
            status = 110;
        }
    }
    if (fesetenv(&saved) != 0) {
        return 104;
    }
    return status;
}

static void *r_test_foreign_thread(void *argument) {
    int *status = argument;
    *status = r_test_foreign_callback();
    return NULL;
}

static int r_test_entry_status;

static void r_test_hosted_drain(void) {
    static _Bool checked;
    if (checked) {
        r_runtime_hosted_drain();
        return;
    }
    checked = 1;
    pthread_t thread;
    int status = r_test_foreign_callback();
    if (status != 0) {
        r_test_entry_status = status;
        r_runtime_hosted_drain();
        return;
    }
    if (pthread_create(&thread, NULL, r_test_foreign_thread, &status) != 0) {
        r_test_entry_status = 105;
        r_runtime_hosted_drain();
        return;
    }
    if (pthread_join(thread, NULL) != 0) {
        r_test_entry_status = 106;
        r_runtime_hosted_drain();
        return;
    }
    if (status != 0) {
        r_test_entry_status = status;
        r_runtime_hosted_drain();
        return;
    }
    r_runtime_hosted_drain();
}

static int r_test_hosted_finish(int32_t result) {
    return r_runtime_hosted_finish(r_test_entry_status == 0 ? result : r_test_entry_status);
}

#define r_runtime_hosted_drain r_test_hosted_drain
#define r_runtime_hosted_finish r_test_hosted_finish
#include R_TEST_GENERATED_C
#undef r_runtime_hosted_finish

#undef r_runtime_hosted_drain
