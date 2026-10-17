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
    @safety("R-RUNTIME-PTHREAD-DETACH", "thread is joinable and not joined")
    c_int pthread_detach(raw Thread*? thread);
    @safety("R-RUNTIME-PTHREAD-COND-TIMEDWAIT-RELATIVE",
            "mutex is held by this thread; both are initialized; interval addresses a timespec")
    c_int pthread_cond_timedwait_relative_np(raw Condition* condition, raw Mutex* mutex,
                                             raw const TimeSpec* interval);
}

// The number of processors online (unistd.h).
@link(name = "system.libc", kind = "system")
@header("unistd.h")
@abi("r-runtime-system-abi")
extern "C" {
    @c_constant(name = "_SC_NPROCESSORS_ONLN")
    const c_int processors_online = 58i32 as c_int;

    @safety("R-RUNTIME-SYSCONF", "None")
    c_long sysconf(c_int name);
}

// Kernel event queues (sys/event.h): the reactor thread waits on one for timers and, from B13,
// for the readiness of descriptors, child processes and signals.
@link(name = "system.libc", kind = "system")
@header("sys/event.h")
@abi("r-runtime-system-abi")
extern "C" {
    // sys/event.h declares it under #pragma pack(4): no member is aligned beyond four bytes.
    @repr(C, packed)
    @c_type(name = "kevent", kind = "struct")
    struct Event {
        @align(4)
        c_uintptr ident;
        @align(2)
        c_int16 filter;
        @align(2)
        c_uint16 flags;
        @align(4)
        c_uint32 fflags;
        @align(4)
        c_intptr data;
        @align(4)
        raw void*? udata;
    };

    @c_constant(name = "EVFILT_READ")
    const c_int filter_read = -1i32 as c_int;
    @c_constant(name = "EVFILT_WRITE")
    const c_int filter_write = -2i32 as c_int;
    @c_constant(name = "EVFILT_TIMER")
    const c_int filter_timer = -7i32 as c_int;
    @c_constant(name = "EVFILT_USER")
    const c_int filter_user = -10i32 as c_int;
    @c_constant(name = "EV_ADD")
    const c_int event_add = 1i32 as c_int;
    @c_constant(name = "EV_DELETE")
    const c_int event_delete = 2i32 as c_int;
    @c_constant(name = "EV_ONESHOT")
    const c_int event_oneshot = 16i32 as c_int;
    @c_constant(name = "EV_CLEAR")
    const c_int event_clear = 32i32 as c_int;
    @c_constant(name = "EV_UDATA_SPECIFIC")
    const c_int event_udata_specific = 256i32 as c_int;
    @c_constant(name = "EV_ERROR")
    const c_int event_error = 16384i32 as c_int;
    @c_constant(name = "NOTE_TRIGGER")
    const c_int note_trigger = 16777216i32 as c_int;
    @c_constant(name = "NOTE_NSECONDS")
    const c_int note_nanoseconds = 4i32 as c_int;

    @safety("R-RUNTIME-KQUEUE", "None")
    c_int kqueue();
    @safety("R-RUNTIME-KEVENT", "the lists address the given counts of kevent records")
    c_int kevent(c_int queue, raw const Event*? changes, c_int change_count, raw Event*? events,
                 c_int event_count, raw const TimeSpec*? timeout);
}

// Sockets and descriptors (sys/socket.h, sys/un.h, fcntl.h, unistd.h, errno.h): the native calls
// of the socket adapters (runtime/r/socket.r), made on nonblocking close-on-exec descriptors.
@link(name = "system.libc", kind = "system")
@header("sys/socket.h")
@header("sys/un.h")
@header("sys/uio.h")
@header("fcntl.h")
@header("unistd.h")
@header("errno.h")
@abi("r-runtime-system-abi")
extern "C" {
    @repr(C)
    @c_type(name = "sockaddr", kind = "struct")
    struct SocketAddress {
        @c_field(name = "sa_len")
        c_uint8 length;
        @c_field(name = "sa_family")
        c_uint8 family;
        @c_field(name = "sa_data")
        c_char[14] data;
    };

    @repr(C)
    @c_type(name = "sockaddr_storage", kind = "struct")
    struct SocketStorage {
        @c_field(name = "ss_len")
        c_uint8 length;
        @c_field(name = "ss_family")
        c_uint8 family;
        @c_field(name = "__ss_pad1")
        c_char[6] head;
        @c_field(name = "__ss_align")
        c_int64 alignment;
        @c_field(name = "__ss_pad2")
        c_char[112] tail;
    };

    @repr(C)
    @c_type(name = "sockaddr_un", kind = "struct")
    struct LocalAddress {
        @c_field(name = "sun_len")
        c_uint8 length;
        @c_field(name = "sun_family")
        c_uint8 family;
        @c_field(name = "sun_path")
        c_char[104] path;
    };

    @repr(C)
    @c_type(name = "iovec", kind = "struct")
    struct IoVector {
        @c_field(name = "iov_base")
        raw void*? base;
        @c_field(name = "iov_len")
        c_size length;
    };

    @repr(C)
    @c_type(name = "msghdr", kind = "struct")
    struct MessageHeader {
        @c_field(name = "msg_name")
        raw void*? name;
        @c_field(name = "msg_namelen")
        c_uint32 name_length;
        @c_field(name = "msg_iov")
        raw IoVector*? vector;
        @c_field(name = "msg_iovlen")
        c_int vector_length;
        @c_field(name = "msg_control")
        raw void*? control;
        @c_field(name = "msg_controllen")
        c_uint32 control_length;
        @c_field(name = "msg_flags")
        c_int flags;
    };

    @c_constant(name = "AF_UNIX")
    const c_int family_local = 1i32 as c_int;
    @c_constant(name = "AF_INET")
    const c_int family_inet = 2i32 as c_int;
    @c_constant(name = "AF_INET6")
    const c_int family_inet6 = 30i32 as c_int;
    @c_constant(name = "SOCK_STREAM")
    const c_int socket_stream = 1i32 as c_int;
    @c_constant(name = "SOCK_DGRAM")
    const c_int socket_datagram = 2i32 as c_int;
    @c_constant(name = "SOL_SOCKET")
    const c_int socket_level = 65535i32 as c_int;
    @c_constant(name = "SO_ERROR")
    const c_int option_error = 4103i32 as c_int;
    @c_constant(name = "SO_TYPE")
    const c_int option_type = 4104i32 as c_int;
    @c_constant(name = "SO_NOSIGPIPE")
    const c_int option_no_sigpipe = 4130i32 as c_int;
    @c_constant(name = "MSG_TRUNC")
    const c_int message_truncated = 16i32 as c_int;
    @c_constant(name = "F_GETFD")
    const c_int get_descriptor_flags = 1i32 as c_int;
    @c_constant(name = "F_SETFD")
    const c_int set_descriptor_flags = 2i32 as c_int;
    @c_constant(name = "F_GETFL")
    const c_int get_status_flags = 3i32 as c_int;
    @c_constant(name = "F_SETFL")
    const c_int set_status_flags = 4i32 as c_int;
    @c_constant(name = "FD_CLOEXEC")
    const c_int close_on_exec = 1i32 as c_int;
    @c_constant(name = "O_NONBLOCK")
    const c_int nonblocking = 4i32 as c_int;
    @c_constant(name = "ENOENT")
    const c_int error_no_entry = 2i32 as c_int;
    @c_constant(name = "EINTR")
    const c_int error_interrupted = 4i32 as c_int;
    @c_constant(name = "ENOMEM")
    const c_int error_no_memory = 12i32 as c_int;
    @c_constant(name = "EAGAIN")
    const c_int error_again = 35i32 as c_int;
    @c_constant(name = "EWOULDBLOCK")
    const c_int error_would_block = 35i32 as c_int;
    @c_constant(name = "EINPROGRESS")
    const c_int error_in_progress = 36i32 as c_int;
    @c_constant(name = "EALREADY")
    const c_int error_already = 37i32 as c_int;
    @c_constant(name = "EMSGSIZE")
    const c_int error_message_size = 40i32 as c_int;
    @c_constant(name = "EISCONN")
    const c_int error_connected = 56i32 as c_int;

    @safety("R-RUNTIME-SOCKET", "None")
    c_int socket(c_int domain, c_int kind, c_int protocol);
    @safety("R-RUNTIME-CONNECT", "address addresses length bytes of a socket address")
    c_int connect(c_int descriptor, raw const SocketAddress* address, c_uint32 length);
    @safety("R-RUNTIME-ACCEPT", "address addresses *length writable bytes")
    c_int accept(c_int descriptor, raw SocketAddress*? address, raw c_uint32*? length);
    @safety("R-RUNTIME-GETSOCKOPT", "value addresses *length writable bytes")
    c_int getsockopt(c_int descriptor, c_int level, c_int name, raw void*? value,
                     raw c_uint32*? length);
    @safety("R-RUNTIME-SETSOCKOPT", "value addresses length bytes")
    c_int setsockopt(c_int descriptor, c_int level, c_int name, raw const void*? value,
                     c_uint32 length);
    @safety("R-RUNTIME-SENDTO", "data addresses size bytes; address addresses length bytes")
    c_long sendto(c_int descriptor, raw const void*? data, c_size size, c_int flags,
                  raw const SocketAddress*? address, c_uint32 length);
    @safety("R-RUNTIME-RECVMSG", "message addresses a valid message header")
    c_long recvmsg(c_int descriptor, raw MessageHeader* message, c_int flags);
    @safety("R-RUNTIME-CLOSE", "None")
    c_int close(c_int descriptor);
    @safety("R-RUNTIME-FCNTL", "the variadic argument is the int the command takes, if any")
    c_int fcntl(c_int descriptor, c_int command, ...);
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
