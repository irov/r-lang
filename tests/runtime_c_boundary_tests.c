#include "r_runtime_0_1.h"
#include "r_runtime_thread_attachment.h"

#include <fenv.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#pragma STDC FENV_ACCESS ON

#define R_BOUNDARY_CHECK(condition)                                                                \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(                                                                         \
                stderr, "C boundary check failed at line %d: %s\n", __LINE__, #condition);         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int r_test_c_call_environment(void) {
    fenv_t original;
    RRuntimeCEnvironment outer;
    RRuntimeCEnvironment inner;

    R_BOUNDARY_CHECK(fegetenv(&original) == 0);
    R_BOUNDARY_CHECK(fesetenv(FE_DFL_ENV) == 0);
    R_BOUNDARY_CHECK(fesetround(FE_UPWARD) == 0);
    R_BOUNDARY_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    r_runtime_c_call_begin(&outer);
    R_BOUNDARY_CHECK(fegetround() == FE_TONEAREST);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
    R_BOUNDARY_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_BOUNDARY_CHECK(feraiseexcept(FE_INVALID) == 0);
    r_runtime_c_call_begin(&inner);
    R_BOUNDARY_CHECK(fegetround() == FE_TONEAREST);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
    R_BOUNDARY_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_BOUNDARY_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    r_runtime_c_call_end(&inner);
    R_BOUNDARY_CHECK(fegetround() == FE_DOWNWARD);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
    r_runtime_c_call_end(&outer);
    R_BOUNDARY_CHECK(fegetround() == FE_UPWARD);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    R_BOUNDARY_CHECK(fesetenv(&original) == 0);
    return 0;
}

static int r_test_c_entry_roundtrip(void) {
    fenv_t original;
    RRuntimeCEntryGuard outer;
    RRuntimeCEntryGuard inner;
    RRuntimeThreadAttachResult attachment;

    R_BOUNDARY_CHECK(fegetenv(&original) == 0);
    R_BOUNDARY_CHECK(fesetenv(FE_DFL_ENV) == 0);
    R_BOUNDARY_CHECK(fesetround(FE_UPWARD) == 0);
    R_BOUNDARY_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    r_runtime_c_entry_begin(&outer);
    R_BOUNDARY_CHECK(outer.active && outer.owns_attachment);
    R_BOUNDARY_CHECK(fegetround() == FE_TONEAREST);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
    attachment = r_runtime_thread_attach();
    R_BOUNDARY_CHECK(attachment.status == R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED);
    R_BOUNDARY_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_BOUNDARY_CHECK(feraiseexcept(FE_INVALID) == 0);
    r_runtime_c_entry_begin(&inner);
    R_BOUNDARY_CHECK(inner.active && !inner.owns_attachment && inner.previous == &outer);
    R_BOUNDARY_CHECK(fegetround() == FE_TONEAREST);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
    R_BOUNDARY_CHECK(r_test_c_call_environment() == 0);
    r_runtime_c_entry_end(&inner);
    R_BOUNDARY_CHECK(!inner.active && outer.active);
    R_BOUNDARY_CHECK(fegetround() == FE_DOWNWARD);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
    r_runtime_c_entry_end(&outer);
    R_BOUNDARY_CHECK(!outer.active);
    R_BOUNDARY_CHECK(fegetround() == FE_UPWARD);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);

    attachment = r_runtime_thread_attach();
    R_BOUNDARY_CHECK(attachment.status == R_RUNTIME_THREAD_ATTACH_OK);
    R_BOUNDARY_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_BOUNDARY_CHECK(feraiseexcept(FE_INVALID) == 0);
    r_runtime_c_entry_begin(&outer);
    R_BOUNDARY_CHECK(outer.active && !outer.owns_attachment);
    R_BOUNDARY_CHECK(fegetround() == FE_TONEAREST);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
    r_runtime_c_entry_end(&outer);
    R_BOUNDARY_CHECK(attachment.attachment.active);
    R_BOUNDARY_CHECK(fegetround() == FE_DOWNWARD);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
    r_runtime_thread_detach(&attachment.attachment);
    R_BOUNDARY_CHECK(fegetround() == FE_UPWARD);
    R_BOUNDARY_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    R_BOUNDARY_CHECK(fesetenv(&original) == 0);
    return 0;
}

static void *r_test_c_entry_worker(void *data) {
    int *status = data;
    *status = r_test_c_entry_roundtrip();
    return NULL;
}

#if defined(__APPLE__) && defined(__arm64__)
static int r_test_c_target_environment(void) {
    fenv_t original;
    fenv_t canonical;
    fenv_t foreign;
    fenv_t observed;
    RRuntimeCEnvironment environment;
    RRuntimeCEntryGuard entry;
    RRuntimeThreadAttachResult attachment;

    R_BOUNDARY_CHECK(fegetenv(&original) == 0);
    R_BOUNDARY_CHECK(fesetenv(FE_DFL_ENV) == 0);
    R_BOUNDARY_CHECK(fegetenv(&canonical) == 0);
    foreign = canonical;
    /* Darwin exposes FPCR/FPSR in fenv_t; FZ is not changed by fesetround/feclearexcept. */
    foreign.__fpcr |= (unsigned long long)__fpcr_flush_to_zero;
    foreign.__fpsr |= (unsigned long long)FE_DIVBYZERO;
    R_BOUNDARY_CHECK(fesetenv(&foreign) == 0);
    R_BOUNDARY_CHECK(fegetenv(&foreign) == 0);
    r_runtime_c_call_begin(&environment);
    R_BOUNDARY_CHECK(fegetenv(&observed) == 0);
    R_BOUNDARY_CHECK(observed.__fpcr == canonical.__fpcr && observed.__fpsr == canonical.__fpsr);
    r_runtime_c_call_end(&environment);
    R_BOUNDARY_CHECK(fegetenv(&observed) == 0);
    R_BOUNDARY_CHECK(observed.__fpcr == foreign.__fpcr && observed.__fpsr == foreign.__fpsr);
    r_runtime_c_entry_begin(&entry);
    R_BOUNDARY_CHECK(fegetenv(&observed) == 0);
    R_BOUNDARY_CHECK(observed.__fpcr == canonical.__fpcr && observed.__fpsr == canonical.__fpsr);
    r_runtime_c_entry_end(&entry);
    R_BOUNDARY_CHECK(fegetenv(&observed) == 0);
    R_BOUNDARY_CHECK(observed.__fpcr == foreign.__fpcr && observed.__fpsr == foreign.__fpsr);
    attachment = r_runtime_thread_attach();
    R_BOUNDARY_CHECK(attachment.status == R_RUNTIME_THREAD_ATTACH_OK);
    R_BOUNDARY_CHECK(fegetenv(&observed) == 0);
    R_BOUNDARY_CHECK(observed.__fpcr == canonical.__fpcr && observed.__fpsr == canonical.__fpsr);
    r_runtime_thread_detach(&attachment.attachment);
    R_BOUNDARY_CHECK(fegetenv(&observed) == 0);
    R_BOUNDARY_CHECK(observed.__fpcr == foreign.__fpcr && observed.__fpsr == foreign.__fpsr);
    R_BOUNDARY_CHECK(fesetenv(&original) == 0);
    return 0;
}
#endif

static int r_test_c_entry_contract(unsigned mode) {
    const pid_t child = fork();
    int status;

    R_BOUNDARY_CHECK(child >= 0);
    if (child == 0) {
        const struct rlimit core_limit = {0, 0};
        RRuntimeCEntryGuard outer;
        RRuntimeCEntryGuard inner;
        RRuntimeThreadAttachResult attached;

        (void)setrlimit(RLIMIT_CORE, &core_limit);
        if (!r_runtime_thread_lifecycle_start()) {
            _Exit(2);
        }
        if (mode == 0U) {
            r_runtime_c_entry_begin(&outer);
            r_runtime_c_entry_begin(&inner);
            r_runtime_c_entry_end(&outer);
        } else if (mode == 1U) {
            attached = r_runtime_thread_attach();
            if (attached.status != R_RUNTIME_THREAD_ATTACH_OK) {
                _Exit(3);
            }
            r_runtime_c_entry_begin(&outer);
            r_runtime_thread_detach(&attached.attachment);
        } else if (mode == 2U) {
            r_runtime_c_entry_begin(&outer);
            r_runtime_thread_detach(&outer.attachment);
        } else {
            r_runtime_c_entry_begin(&outer);
            r_runtime_c_entry_begin(&outer);
        }
        _Exit(4);
    }
    R_BOUNDARY_CHECK(waitpid(child, &status, 0) == child);
    R_BOUNDARY_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    return 0;
}

/* A synchronous callback from a destructor stays on the owning R thread. */
static unsigned r_test_cleanup_count;

static void r_test_count_cleanup(void) {
    r_test_cleanup_count += 1U;
}

static int r_test_destructor_callback_after_drain(void) {
    RRuntimeCEnvironment call;
    RRuntimeCEntryGuard entry;
    r_runtime_hosted_work_drain();
    R_BOUNDARY_CHECK(r_runtime_thread_attach().status == R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING);
    r_runtime_thread_local_cleanup_install(r_test_count_cleanup);
    r_runtime_c_call_begin(&call);
    r_runtime_c_entry_begin(&entry);
    r_runtime_c_entry_end(&entry);
    r_runtime_c_call_end(&call);
    R_BOUNDARY_CHECK(r_test_cleanup_count == 0U);
    r_runtime_thread_local_cleanup_current();
    R_BOUNDARY_CHECK(r_test_cleanup_count == 1U);
    r_runtime_thread_local_cleanup_install(NULL);
    r_runtime_hosted_work_drain();
    return 0;
}

int main(void) {
    pthread_t workers[4];
    int statuses[4] = {0};

    R_BOUNDARY_CHECK(r_test_c_call_environment() == 0);
    R_BOUNDARY_CHECK(r_runtime_thread_lifecycle_start());
    R_BOUNDARY_CHECK(r_test_c_entry_roundtrip() == 0);
#if defined(__APPLE__) && defined(__arm64__)
    R_BOUNDARY_CHECK(r_test_c_target_environment() == 0);
#endif
    for (size_t index = 0U; index < 4U; ++index) {
        R_BOUNDARY_CHECK(
            pthread_create(&workers[index], NULL, r_test_c_entry_worker, &statuses[index]) == 0);
    }
    for (size_t index = 0U; index < 4U; ++index) {
        R_BOUNDARY_CHECK(pthread_join(workers[index], NULL) == 0);
        R_BOUNDARY_CHECK(statuses[index] == 0);
    }
    R_BOUNDARY_CHECK(r_test_destructor_callback_after_drain() == 0);
    r_runtime_thread_lifecycle_stop();
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        R_BOUNDARY_CHECK(r_test_c_entry_contract(mode) == 0);
    }
    (void)puts("runtime C boundary tests: ok");
    return 0;
}
