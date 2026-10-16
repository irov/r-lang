module runtime.threads;
import runtime.types;
import runtime.types::{SourceSpan};
import runtime.system;
import runtime.system::{Mutex, Condition, FloatingEnvironment};
import runtime.panics;
import runtime.stacks;

// The threads that run R code (r_runtime_thread_attachment.h): the hosted lifecycle of their
// attachments, the floating-point environment of C callers, which R code runs without, the guards
// of C entries into exports, and the hosted work that the drain waits for (r_runtime_0_1.h).
@header("r_runtime_thread_attachment.h")
@header("r_runtime_0_1.h")
@header("r_runtime_core.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_thread_attachment.h")
@abi("r-runtime-abi")
extern "C" {
    @repr(C)
    @c_type(name = "RRuntimeThreadAttachStatus", kind = "typedef")
    enum AttachStatus : c_uint {
        R_RUNTIME_THREAD_ATTACH_OK = 0,
        R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING = 1,
        R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED = 2,
        R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE = 3,
    };

    @repr(C)
    @c_type(name = "RRuntimeThreadAttachment", kind = "typedef")
    struct Attachment {
        FloatingEnvironment caller_environment;
        c_uint64 generation;
        c_bool active;
    };

    @repr(C)
    @c_type(name = "RRuntimeThreadAttachResult", kind = "typedef")
    struct AttachResult {
        AttachStatus status;
        Attachment attachment;
    };

    @repr(C)
    @c_type(name = "RRuntimeCEnvironment", kind = "typedef")
    struct CEnvironment {
        FloatingEnvironment environment;
    };

    @repr(C)
    @c_type(name = "RRuntimeCEntryGuard", kind = "typedef")
    struct CEntryGuard {
        Attachment attachment;
        FloatingEnvironment environment;
        c_bool owns_attachment;
        c_bool active;
        raw CEntryGuard*? previous;
    };
}

enum Lifecycle {
    stopped,
    running,
    stopping,
};

Mutex lifecycle_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
Condition lifecycle_condition = {.signature = runtime.system::condition_signature as c_long,
                                 .bytes = {}};
Lifecycle lifecycle = Lifecycle::stopped;
u64 generation = 1u64;
usize attachment_count = 0usize;
usize hosted_work_count = 0usize;
bool external_entry_closed = false;
thread_local usize c_call_depth = 0usize;
thread_local bool current_attached = false;
thread_local usize c_entry_depth = 0usize;
thread_local raw CEntryGuard*? c_entry_top = null;
atomic raw fn?() -> void thread_local_cleanup = null;

protected never contract_violation() {
    runtime.panics::r_runtime_panic(
        13u32 as c_uint,
        SourceSpan {.module_id = 0u32 as c_uint32, .start = 0u32 as c_uint32,
                    .end = 0u32 as c_uint32});
}

protected void lifecycle_lock() {
    unsafe {
        if (runtime.system::pthread_mutex_lock(&lifecycle_mutex as raw Mutex*) != 0i32 as c_int) {
            contract_violation();
        }
    }
}

protected void lifecycle_unlock() {
    unsafe {
        if (runtime.system::pthread_mutex_unlock(&lifecycle_mutex as raw Mutex*) !=
            0i32 as c_int) {
            contract_violation();
        }
    }
}

protected void lifecycle_wait() {
    unsafe {
        if (runtime.system::pthread_cond_wait(&lifecycle_condition as raw Condition*,
                                              &lifecycle_mutex as raw Mutex*) != 0i32 as c_int) {
            lifecycle_unlock();
            contract_violation();
        }
    }
}

protected void lifecycle_broadcast() {
    unsafe {
        if (runtime.system::pthread_cond_broadcast(&lifecycle_condition as raw Condition*) !=
            0i32 as c_int) {
            contract_violation();
        }
    }
}

// Whether the floating-point environment could be read into, or set from, environment.
protected bool get_environment(raw FloatingEnvironment* environment) {
    unsafe {
        return runtime.system::fegetenv(environment) == 0i32 as c_int;
    }
}

protected bool set_environment(raw const FloatingEnvironment* environment) {
    unsafe {
        return runtime.system::fesetenv(environment) == 0i32 as c_int;
    }
}

protected bool set_default_environment() {
    unsafe {
        return set_environment(runtime.system::_FE_DFL_ENV);
    }
}

// Restores a saved environment where failing to restore it leaves no way to continue.
protected void restore_environment(raw const FloatingEnvironment* environment) {
    if (set_environment(environment) == false) {
        unsafe {
            runtime.system::abort();
        }
    }
}

protected FloatingEnvironment no_environment() {
    return FloatingEnvironment {.status = 0u64 as c_ullong, .control = 0u64 as c_ullong};
}

protected Attachment no_attachment() {
    return Attachment {.caller_environment = no_environment(), .generation = 0u64 as c_uint64,
                       .active = runtime.types::c_bool_of(false)};
}

@safety("R-RUNTIME-THREAD-LIFECYCLE-START", "None")
extern "C" c_bool r_runtime_thread_lifecycle_start() {
    lifecycle_lock();
    unsafe {
        bool idle = (lifecycle == Lifecycle::stopped) && (attachment_count == 0usize);
        if (idle == true) {
            lifecycle = Lifecycle::running;
            external_entry_closed = false;
        }
        lifecycle_unlock();
        return runtime.types::c_bool_of(idle);
    }
}

@safety("R-RUNTIME-THREAD-LIFECYCLE-STOP", "None")
extern "C" void r_runtime_thread_lifecycle_stop() {
    if (current_attached == true) {
        contract_violation();
    }
    lifecycle_lock();
    unsafe {
        if (lifecycle != Lifecycle::running) {
            lifecycle_unlock();
            contract_violation();
        }
        lifecycle = Lifecycle::stopping;
        while (attachment_count != 0usize) {
            lifecycle_wait();
        }
        lifecycle = Lifecycle::stopped;
        if (generation == 18446744073709551615u64) {
            lifecycle_unlock();
            contract_violation();
        }
        generation += 1u64;
    }
    lifecycle_unlock();
}

protected void release_reservation() {
    lifecycle_lock();
    unsafe {
        if (attachment_count == 0usize) {
            lifecycle_unlock();
            contract_violation();
        }
        attachment_count -= 1usize;
    }
    lifecycle_broadcast();
    lifecycle_unlock();
}

@safety("R-RUNTIME-THREAD-ATTACH", "None")
extern "C" AttachResult r_runtime_thread_attach() {
    lifecycle_lock();
    unsafe {
        bool closed = (external_entry_closed == true) && (c_call_depth == 0usize);
        if ((lifecycle != Lifecycle::running) || (closed == true)) {
            lifecycle_unlock();
            return AttachResult {.status = AttachStatus::R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING,
                                 .attachment = no_attachment()};
        }
        if ((current_attached == true) || (attachment_count == runtime.types::size_maximum)) {
            lifecycle_unlock();
            return AttachResult {
                .status = AttachStatus::R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED,
                .attachment = no_attachment()};
        }
        attachment_count += 1usize;
        AttachResult result = {.status = AttachStatus::R_RUNTIME_THREAD_ATTACH_OK,
                               .attachment = no_attachment()};
        result.attachment.generation = generation as c_uint64;
        lifecycle_unlock();

        raw FloatingEnvironment* caller =
            &result.attachment.caller_environment as raw FloatingEnvironment*;
        if (get_environment(caller) == false) {
            release_reservation();
            result.status =
                AttachStatus::R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE;
            return result;
        }
        if (set_default_environment() == false) {
            restore_environment(caller as raw const FloatingEnvironment*);
            release_reservation();
            result.status =
                AttachStatus::R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE;
            return result;
        }
        if (runtime.types::is_true(runtime.stacks::r_runtime_stack_initialize_current_thread()) ==
            false) {
            restore_environment(caller as raw const FloatingEnvironment*);
            release_reservation();
            result.status = AttachStatus::R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED;
            return result;
        }
        current_attached = true;
        result.attachment.active = runtime.types::c_bool_of(true);
        return result;
    }
}

@safety("R-RUNTIME-THREAD-LOCAL-CLEANUP-INSTALL", "cleanup is null or runs on any thread")
extern "C" void r_runtime_thread_local_cleanup_install(raw fn?() -> void cleanup) {
    core::atomic_store(&thread_local_cleanup, cleanup, core::memory_order::release);
}

@safety("R-RUNTIME-THREAD-LOCAL-CLEANUP-CURRENT", "None")
extern "C" void r_runtime_thread_local_cleanup_current() {
    raw fn?() -> void cleanup =
        core::atomic_load(&thread_local_cleanup, core::memory_order::acquire);
    unsafe {
        if (cleanup != null) {
            cleanup();
        }
    }
}

@safety("R-RUNTIME-THREAD-DETACH", "attachment is the active attachment of this thread")
extern "C" void r_runtime_thread_detach(raw Attachment*? attachment) {
    unsafe {
        if (attachment == null) {
            contract_violation();
        }
        bool inactive = runtime.types::is_true(attachment->active) == false;
        if ((inactive == true) || (current_attached == false) || (c_entry_depth != 0usize)) {
            contract_violation();
        }
        lifecycle_lock();
        bool live = (lifecycle == Lifecycle::running) || (lifecycle == Lifecycle::stopping);
        if (((attachment->generation as u64) != generation) || (live == false)) {
            lifecycle_unlock();
            contract_violation();
        }
        lifecycle_unlock();
        // A synchronous R -> C -> R callback returns to its still-live R thread, whose
        // thread-local objects stay, also during the destruction of the initial thread.
        if (c_call_depth == 0usize) {
            r_runtime_thread_local_cleanup_current();
        }
        if (set_environment(&attachment->caller_environment as raw const FloatingEnvironment*) ==
            false) {
            contract_violation();
        }
        attachment->active = runtime.types::c_bool_of(false);
    }
    current_attached = false;
    release_reservation();
}

@safety("R-RUNTIME-C-CALL-BEGIN", "environment is writable storage of the call")
extern "C" void r_runtime_c_call_begin(raw CEnvironment*? environment) {
    if (c_call_depth == runtime.types::size_maximum) {
        contract_violation();
    }
    c_call_depth += 1usize;
    unsafe {
        if (environment == null) {
            contract_violation();
        }
        raw FloatingEnvironment* saved =
            &environment->environment as raw FloatingEnvironment*;
        if (get_environment(saved) == false) {
            contract_violation();
        }
        if (set_default_environment() == false) {
            restore_environment(saved as raw const FloatingEnvironment*);
            contract_violation();
        }
    }
}

@safety("R-RUNTIME-C-CALL-END", "environment was begun by this thread's innermost call")
extern "C" void r_runtime_c_call_end(raw CEnvironment*? environment) {
    if (c_call_depth == 0usize) {
        contract_violation();
    }
    c_call_depth -= 1usize;
    unsafe {
        if (environment == null) {
            contract_violation();
        }
        restore_environment(&environment->environment as raw const FloatingEnvironment*);
    }
}

@safety("R-RUNTIME-C-ENTRY-BEGIN", "guard is fresh storage of the entry")
extern "C" void r_runtime_c_entry_begin(raw CEntryGuard*? guard) {
    unsafe {
        if ((guard == null) || (c_entry_depth == runtime.types::size_maximum)) {
            contract_violation();
        }
        raw CEntryGuard*? entry = c_entry_top;
        while (entry != null) {
            if (entry == guard) {
                contract_violation();
            }
            entry = entry->previous;
        }
        *guard = CEntryGuard {.attachment = no_attachment(), .environment = no_environment(),
                              .owns_attachment = runtime.types::c_bool_of(false),
                              .active = runtime.types::c_bool_of(false), .previous = null};
        if (current_attached == false) {
            AttachResult attached = r_runtime_thread_attach();
            if (attached.status == AttachStatus::R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING) {
                // R-FFI-0055, R-FFI-0064: an export entered before the runtime started (a library
                // before NAME_initialize) or after its shutdown; the caller broke the contract.
                contract_violation();
            }
            if (attached.status != AttachStatus::R_RUNTIME_THREAD_ATTACH_OK) {
                runtime.system::abort();
            }
            guard->attachment = attached.attachment;
            guard->environment = attached.attachment.caller_environment;
            guard->owns_attachment = runtime.types::c_bool_of(true);
        } else {
            raw FloatingEnvironment* saved = &guard->environment as raw FloatingEnvironment*;
            if (get_environment(saved) == false) {
                runtime.system::abort();
            }
            if (set_default_environment() == false) {
                runtime.system::abort();
            }
        }
        guard->previous = c_entry_top;
        guard->active = runtime.types::c_bool_of(true);
    }
    c_entry_top = guard;
    c_entry_depth += 1usize;
}

@safety("R-RUNTIME-C-ENTRY-END", "guard is the innermost entry of this thread")
extern "C" void r_runtime_c_entry_end(raw CEntryGuard*? guard) {
    unsafe {
        if ((guard == null) || (guard != c_entry_top)) {
            contract_violation();
        }
        bool owns = runtime.types::is_true(guard->owns_attachment);
        bool inactive = runtime.types::is_true(guard->active) == false;
        bool nested = (owns == true) && (c_entry_depth != 1usize);
        if ((inactive == true) || (c_entry_depth == 0usize) || (current_attached == false) ||
            (nested == true)) {
            contract_violation();
        }
        c_entry_top = guard->previous;
        c_entry_depth -= 1usize;
        guard->active = runtime.types::c_bool_of(false);
        guard->previous = null;
        if (owns == true) {
            r_runtime_thread_detach(&guard->attachment as raw Attachment*);
            guard->owns_attachment = runtime.types::c_bool_of(false);
        } else {
            restore_environment(&guard->environment as raw const FloatingEnvironment*);
        }
    }
}

@safety("R-RUNTIME-HOSTED-WORK-BEGIN", "None")
extern "C" void r_runtime_hosted_work_begin() {
    lifecycle_lock();
    unsafe {
        if (hosted_work_count == runtime.types::size_maximum) {
            contract_violation();
        }
        hosted_work_count += 1usize;
    }
    lifecycle_unlock();
}

@safety("R-RUNTIME-HOSTED-WORK-END", "a work reservation of this runtime is held")
extern "C" void r_runtime_hosted_work_end() {
    lifecycle_lock();
    unsafe {
        if (hosted_work_count == 0usize) {
            contract_violation();
        }
        hosted_work_count -= 1usize;
    }
    lifecycle_broadcast();
    lifecycle_unlock();
}

@safety("R-RUNTIME-HOSTED-WORK-DRAIN", "None")
extern "C" void r_runtime_hosted_work_drain() {
    lifecycle_lock();
    unsafe {
        while ((hosted_work_count != 0usize) || (attachment_count != 0usize)) {
            lifecycle_wait();
        }
        external_entry_closed = true;
    }
    lifecycle_unlock();
}
