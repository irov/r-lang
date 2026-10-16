module runtime.system;

// The C interfaces of the system that the runtime written in R uses: threads, locks and
// conditions of pthread, proven against the headers of the target's SDK (R-FFI-0041).
@link(name = "system.libc", kind = "system")
@header("pthread.h")
@abi("r-runtime-system-abi")
extern "C" {
    @c_type(name = "_opaque_pthread_t", kind = "struct")
    opaque struct Thread;

    @repr(C)
    @c_type(name = "_opaque_pthread_mutex_t", kind = "struct")
    struct Mutex {
        @c_field(name = "__sig")
        c_long signature;
        @c_field(name = "__opaque")
        c_char[56] bytes;
    };

    @repr(C)
    @c_type(name = "_opaque_pthread_cond_t", kind = "struct")
    struct Condition {
        @c_field(name = "__sig")
        c_long signature;
        @c_field(name = "__opaque")
        c_char[40] bytes;
    };

    // The signatures of PTHREAD_MUTEX_INITIALIZER and PTHREAD_COND_INITIALIZER.
    @c_constant(name = "_PTHREAD_MUTEX_SIG_init")
    const c_int mutex_signature = 0x32AAABA7i32 as c_int;
    @c_constant(name = "_PTHREAD_COND_SIG_init")
    const c_int condition_signature = 0x3CB0B1BBi32 as c_int;

    @safety("R-RUNTIME-PTHREAD-SELF", "None")
    raw Thread* pthread_self();
    @safety("R-RUNTIME-PTHREAD-STACKADDR", "thread is a live thread")
    raw void*? pthread_get_stackaddr_np(raw Thread* thread);
    @safety("R-RUNTIME-PTHREAD-STACKSIZE", "thread is a live thread")
    c_size pthread_get_stacksize_np(raw Thread* thread);
    @safety("R-RUNTIME-PTHREAD-MUTEX-LOCK", "mutex is initialized")
    c_int pthread_mutex_lock(raw Mutex* mutex);
    @safety("R-RUNTIME-PTHREAD-MUTEX-UNLOCK", "mutex is held by this thread")
    c_int pthread_mutex_unlock(raw Mutex* mutex);
    @safety("R-RUNTIME-PTHREAD-COND-WAIT", "mutex is held by this thread; both are initialized")
    c_int pthread_cond_wait(raw Condition* condition, raw Mutex* mutex);
    @safety("R-RUNTIME-PTHREAD-COND-BROADCAST", "condition is initialized")
    c_int pthread_cond_broadcast(raw Condition* condition);
    @safety("R-RUNTIME-PTHREAD-COND-SIGNAL", "condition is initialized")
    c_int pthread_cond_signal(raw Condition* condition);
}

// A mutex and a condition in the state of the static initializers of pthread.h.
const Mutex unlocked_mutex = {.signature = mutex_signature as c_long, .bytes = {}};
const Condition idle_condition = {.signature = condition_signature as c_long, .bytes = {}};

@link(name = "system.libc", kind = "system")
@header("pthread.h")
@header("stdlib.h")
@header("time.h")
@header("mach/mach_time.h")
@abi("r-runtime-system-abi")
extern "C" {
    @c_type(name = "_opaque_pthread_attr_t", kind = "struct")
    opaque struct ThreadAttributes;
    @c_type(name = "_opaque_pthread_mutexattr_t", kind = "struct")
    opaque struct MutexAttributes;
    @c_type(name = "_opaque_pthread_condattr_t", kind = "struct")
    opaque struct ConditionAttributes;

    @repr(C)
    @c_type(name = "timespec", kind = "struct")
    struct TimeSpec {
        @c_field(name = "tv_sec")
        c_long seconds;
        @c_field(name = "tv_nsec")
        c_long nanoseconds;
    };

    @repr(C)
    @c_type(name = "mach_timebase_info", kind = "struct")
    struct Timebase {
        c_uint32 numer;
        c_uint32 denom;
    };

    @safety("R-RUNTIME-ABORT", "None")
    never abort();
    @safety("R-RUNTIME-NANOSLEEP", "interval addresses a timespec")
    c_int nanosleep(raw const TimeSpec* interval, raw TimeSpec*? remaining);
    @safety("R-RUNTIME-MACH-CONTINUOUS-TIME", "None")
    c_uint64 mach_continuous_time();
    @safety("R-RUNTIME-MACH-TIMEBASE-INFO", "info is writable")
    c_int mach_timebase_info(raw Timebase* info);
    @safety("R-RUNTIME-PTHREAD-MUTEX-INIT", "mutex is writable storage")
    c_int pthread_mutex_init(raw Mutex* mutex, raw const MutexAttributes*? attributes);
    @safety("R-RUNTIME-PTHREAD-MUTEX-DESTROY", "mutex is initialized and unlocked")
    c_int pthread_mutex_destroy(raw Mutex* mutex);
    @safety("R-RUNTIME-PTHREAD-COND-INIT", "condition is writable storage")
    c_int pthread_cond_init(raw Condition* condition, raw const ConditionAttributes*? attributes);
    @safety("R-RUNTIME-PTHREAD-COND-DESTROY", "condition is initialized without waiters")
    c_int pthread_cond_destroy(raw Condition* condition);
    @safety("R-RUNTIME-PTHREAD-CREATE", "start runs with argument on the new thread")
    c_int pthread_create(raw (raw Thread*?)* thread, raw const ThreadAttributes*? attributes,
                         raw fn(raw void*?) -> raw void*? start, raw void*? argument);
    @safety("R-RUNTIME-PTHREAD-JOIN", "thread is joinable and not joined")
    c_int pthread_join(raw Thread*? thread, raw (raw void*?)*? result);
    @safety("R-RUNTIME-PTHREAD-SETNAME", "name is a NUL-terminated string")
    c_int pthread_setname_np(raw const c_char* name);
}

// The floating-point environment (fenv.h): R code runs in the default environment, and a C caller's
// is restored when its call returns.
@link(name = "system.libc", kind = "system")
@header("fenv.h")
@abi("r-runtime-system-abi")
extern "C" {
    @repr(C)
    @c_type(name = "fenv_t", kind = "typedef")
    struct FloatingEnvironment {
        @c_field(name = "__fpsr")
        c_ullong status;
        @c_field(name = "__fpcr")
        c_ullong control;
    };

    // FE_DFL_ENV is the address of this object.
    @c_address
    raw const FloatingEnvironment* _FE_DFL_ENV;

    @safety("R-RUNTIME-FEGETENV", "environment is writable")
    c_int fegetenv(raw FloatingEnvironment* environment);
    @safety("R-RUNTIME-FESETENV", "environment addresses an environment")
    c_int fesetenv(raw const FloatingEnvironment* environment);
}

// Dispatch (libdispatch). Its objects are pointers to opaque structs; dispatch_object_t is a
// transparent union of them, passed as the pointer it holds.
@link(name = "system.libc", kind = "system")
@header("dispatch/dispatch.h")
@abi("r-runtime-system-abi")
extern "C" {
    @c_type(name = "_os_object_s", kind = "struct") opaque struct OsObject;
    @c_type(name = "dispatch_object_s", kind = "struct") opaque struct DispatchObject;
    @c_type(name = "dispatch_queue_s", kind = "struct") opaque struct Queue;
    @c_type(name = "dispatch_queue_attr_s", kind = "struct") opaque struct QueueAttributes;
    @c_type(name = "dispatch_group_s", kind = "struct") opaque struct Group;
    @c_type(name = "dispatch_source_s", kind = "struct") opaque struct Source;
    @c_type(name = "dispatch_channel_s", kind = "struct") opaque struct Channel;
    @c_type(name = "dispatch_mach_s", kind = "struct") opaque struct Mach;
    @c_type(name = "dispatch_mach_msg_s", kind = "struct") opaque struct MachMessage;
    @c_type(name = "dispatch_semaphore_s", kind = "struct") opaque struct Semaphore;
    @c_type(name = "dispatch_data_s", kind = "struct") opaque struct Data;
    @c_type(name = "dispatch_io_s", kind = "struct") opaque struct Io;
    @c_type(name = "dispatch_source_type_s", kind = "struct") opaque struct SourceType;

    @repr(C)
    @c_type(name = "dispatch_object_t", kind = "typedef")
    union Object {
        @c_field(name = "_os_obj") raw OsObject*? os_object;
        @c_field(name = "_do") raw DispatchObject*? object;
        @c_field(name = "_dq") raw Queue*? queue;
        @c_field(name = "_dqa") raw QueueAttributes*? queue_attributes;
        @c_field(name = "_dg") raw Group*? group;
        @c_field(name = "_ds") raw Source*? source;
        @c_field(name = "_dch") raw Channel*? channel;
        @c_field(name = "_dm") raw Mach*? mach;
        @c_field(name = "_dmsg") raw MachMessage*? mach_message;
        @c_field(name = "_dsema") raw Semaphore*? semaphore;
        @c_field(name = "_ddata") raw Data*? data;
        @c_field(name = "_dchannel") raw Io*? io;
    };

    // DISPATCH_SOURCE_TYPE_TIMER and DISPATCH_QUEUE_CONCURRENT are the addresses of these objects.
    @c_address
    raw const SourceType* _dispatch_source_type_timer;
    @c_address
    raw QueueAttributes* _dispatch_queue_attr_concurrent;

    @c_constant(name = "DISPATCH_TIME_NOW")
    const c_ullong time_now = 0u64 as c_ullong;
    @c_constant(name = "DISPATCH_TIME_FOREVER")
    const c_ullong time_forever = 18446744073709551615u64 as c_ullong;
    @c_constant(name = "DISPATCH_QUEUE_PRIORITY_DEFAULT")
    const c_int priority_default = 0i32 as c_int;
    @c_constant(name = "DISPATCH_TIMER_STRICT")
    const c_int timer_strict = 1i32 as c_int;

    @safety("R-RUNTIME-DISPATCH-QUEUE-CREATE", "label is null or NUL-terminated")
    raw Queue*? dispatch_queue_create(raw const c_char*? label, raw QueueAttributes*? attributes);
    @safety("R-RUNTIME-DISPATCH-GLOBAL-QUEUE", "None")
    raw Queue*? dispatch_get_global_queue(c_intptr identifier, c_uintptr flags);
    @safety("R-RUNTIME-DISPATCH-ASYNC", "work may run with context on any thread")
    void dispatch_async_f(raw Queue*? queue, raw void*? context, raw fn(raw void*) -> void work);
    @safety("R-RUNTIME-DISPATCH-AFTER", "work may run with context on any thread")
    void dispatch_after_f(c_uint64 when, raw Queue*? queue, raw void*? context,
                          raw fn(raw void*) -> void work);
    @safety("R-RUNTIME-DISPATCH-TIME", "None")
    c_uint64 dispatch_time(c_uint64 when, c_int64 delta);
    @safety("R-RUNTIME-DISPATCH-RELEASE", "object is a live dispatch object")
    void dispatch_release(Object object);
    @safety("R-RUNTIME-DISPATCH-ACTIVATE", "object is an inactive dispatch object")
    void dispatch_activate(Object object);
    @safety("R-RUNTIME-DISPATCH-SET-CONTEXT", "object is a live dispatch object")
    void dispatch_set_context(Object object, raw void*? context);
    @safety("R-RUNTIME-DISPATCH-SOURCE-CREATE", "type is a source type of dispatch")
    raw Source*? dispatch_source_create(raw const SourceType* type, c_uintptr handle,
                                        c_uintptr mask, raw Queue*? queue);
    @safety("R-RUNTIME-DISPATCH-SOURCE-SET-TIMER", "source is a timer source")
    void dispatch_source_set_timer(raw Source*? source, c_uint64 start, c_uint64 interval,
                                   c_uint64 leeway);
    @safety("R-RUNTIME-DISPATCH-SOURCE-SET-EVENT-HANDLER", "handler may run with the context")
    void dispatch_source_set_event_handler_f(raw Source*? source, raw fn?(raw void*) -> void handler);
    @safety("R-RUNTIME-DISPATCH-SOURCE-SET-CANCEL-HANDLER", "handler may run with the context")
    void dispatch_source_set_cancel_handler_f(raw Source*? source, raw fn?(raw void*) -> void handler);
    @safety("R-RUNTIME-DISPATCH-SOURCE-CANCEL", "source is a live source")
    void dispatch_source_cancel(raw Source*? source);
}

// A NUL-terminated C string from a constant ending in "\0".
raw const c_char* c_string(str text) {
    const u8[] bytes = text;
    unsafe {
        raw const u8* first = &bytes[0usize] as raw const u8*;
        return (first as raw const void*) as raw const c_char*;
    }
}

// Aborts when a C call that cannot fail on a correct runtime reports an error.
void require_zero(c_int status) {
    if (status != 0i32 as c_int) {
        unsafe {
            abort();
        }
    }
}
