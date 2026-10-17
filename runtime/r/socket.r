module runtime.sockets;
import runtime.types;
import runtime.types::{Allocator, AllocationStatus};
import runtime.system;
import runtime.system::{Mutex, Condition, SocketAddress, SocketStorage, LocalAddress, IoVector,
                        MessageHeader};
import runtime.heap;
import runtime.reactor;
import runtime.reactor::{Source, Job};
import runtime.tasks;

// The socket adapters of darwin (r_runtime_darwin_socket.h): a nonblocking connect, accept or
// datagram transfer waits for the readiness of its descriptor on a watch of the reactor and, with
// a deadline, on an alarm fixed at prepare. The native call runs in a job of the worker pool,
// never on the reactor thread, so a paused or slow call delays no other source. The completion
// is delivered once, by a second job, after the terminal outcome is selected, both sources have
// settled and no native job runs: it is the last access of the adapter to the request, as the
// cancel handlers of the Dispatch sources were.
@header("r_runtime_darwin_socket.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_darwin_socket.h")
@abi("r-runtime-abi")
extern "C" {
    @c_type(name = "RRuntimeDarwinSocketConnect", kind = "typedef")
    opaque struct ConnectHandle;
    @c_type(name = "RRuntimeDarwinSocketAccept", kind = "typedef")
    opaque struct AcceptHandle;
    @c_type(name = "RRuntimeDarwinSocketDatagram", kind = "typedef")
    opaque struct DatagramHandle;

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketPrepareStatus", kind = "typedef")
    enum PrepareStatus : c_uint {
        R_RUNTIME_DARWIN_SOCKET_PREPARE_OK = 0,
        R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID = 1,
        R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED = 2,
        R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED = 3,
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketTerminalEvent", kind = "typedef")
    enum TerminalEvent : c_uint {
        R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE = 0,
        R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED = 1,
        R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT = 2,
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketDatagramOperation", kind = "typedef")
    enum DatagramOperation : c_uint {
        R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND = 0,
        R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE = 1,
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketPrepareResult", kind = "typedef")
    struct ConnectPrepared {
        raw ConnectHandle*? request;
        PrepareStatus status;
        c_int native_error;
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketConnectResult", kind = "typedef")
    struct ConnectResult {
        TerminalEvent terminal_event;
        c_int native_error;
        c_uint64 terminal_event_sequence;
        c_bool connected;
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketAcceptPrepareResult", kind = "typedef")
    struct AcceptPrepared {
        raw AcceptHandle*? request;
        PrepareStatus status;
        c_int native_error;
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketAcceptResult", kind = "typedef")
    struct AcceptResult {
        TerminalEvent terminal_event;
        c_int native_error;
        c_uint64 terminal_event_sequence;
        SocketStorage peer;
        c_uint32 peer_length;
        c_bool accepted;
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketDatagramPrepareResult", kind = "typedef")
    struct DatagramPrepared {
        raw DatagramHandle*? request;
        PrepareStatus status;
        c_int native_error;
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinSocketDatagramResult", kind = "typedef")
    struct DatagramResult {
        DatagramOperation operation;
        TerminalEvent terminal_event;
        c_int native_error;
        c_uint64 terminal_event_sequence;
        SocketStorage peer;
        c_uint32 peer_length;
        c_size count;
        c_bool completed;
        c_bool truncated;
    };
}

// The sizes of sockaddr_in and sockaddr_in6 on the target.
const u32 inet_length = 16u32;
const u32 inet6_length = 28u32;
const u64 i64_maximum = 9223372036854775807u64;
const u64 u64_maximum = 18446744073709551615u64;

enum Kind { connect, accept, datagram };

// One connect, accept or datagram transfer. Written under its mutex; the reactor callbacks and
// the jobs take it, never while holding the reactor lock, and post commands while holding it.
struct Request {
    Mutex mutex = {.signature = 0i64 as c_long, .bytes = {}};
    raw Allocator*? allocator = null;
    Kind kind = Kind::connect;
    // The socket a connect owns, or the listener or datagram socket a request borrows.
    c_int descriptor = -1i32 as c_int;
    // The descriptor an accept took, owned until it is taken or the request ends.
    c_int accepted = -1i32 as c_int;
    // The remote of a connect, the peer of a datagram send (length zero: the connected peer).
    SocketStorage address = {.length = 0u8 as c_uint8, .family = 0u8 as c_uint8, .head = {},
                             .alignment = 0i64 as c_int64, .tail = {}};
    c_uint32 address_length = 0u32 as c_uint32;
    DatagramOperation operation = DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND;
    raw const void*? send_data = null;
    raw void*? receive_data = null;
    c_size size = 0u64 as c_size;
    Source ready = {.next = null, .fire = null, .settle = null, .context = null,
                    .deadline = 0u64 as c_uint64, .ident = 0u64 as c_uintptr,
                    .filter = runtime.system::filter_timer, .command = 0u32 as c_uint32,
                    .state = 0u32 as c_uint32};
    Source alarm = {.next = null, .fire = null, .settle = null, .context = null,
                    .deadline = 0u64 as c_uint64, .ident = 0u64 as c_uintptr,
                    .filter = runtime.system::filter_timer, .command = 0u32 as c_uint32,
                    .state = 0u32 as c_uint32};
    bool timed = false;
    u64 deadline = 0u64;
    Job native = {.next = null, .run = null, .context = null};
    Job delivery = {.next = null, .run = null, .context = null};
    raw fn?(raw ConnectHandle*?, raw void*?) -> void connect_completion = null;
    raw fn?(raw AcceptHandle*?, raw void*?) -> void accept_completion = null;
    raw fn?(raw DatagramHandle*?, raw void*?) -> void datagram_completion = null;
    raw void*? completion_context = null;
    bool bound = false;
    // The outcome, fixed by the terminal selection and published by the delivery.
    TerminalEvent terminal_event = TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE;
    c_int native_error = 0i32 as c_int;
    u64 terminal_sequence = 0u64;
    bool succeeded = false;
    SocketStorage peer = {.length = 0u8 as c_uint8, .family = 0u8 as c_uint8, .head = {},
                          .alignment = 0i64 as c_int64, .tail = {}};
    c_uint32 peer_length = 0u32 as c_uint32;
    c_size count = 0u64 as c_size;
    bool truncated = false;
    // Sources not settled and native jobs not finished; the delivery waits for none.
    u32 pending = 0u32;
    bool ready_activated = false;
    bool alarm_activated = false;
    bool terminal_selected = false;
    bool completion_delivered = false;
    bool descriptor_taken = false;
};

protected never fail() {
    unsafe {
        runtime.system::abort();
    }
}

protected void request_lock(raw Request*? request) {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_lock(&request->mutex as raw Mutex*));
    }
}

protected void request_unlock(raw Request*? request) {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_unlock(&request->mutex as raw Mutex*));
    }
}

protected raw Request*? of_connect(raw ConnectHandle*? handle) {
    unsafe {
        if (handle == null) {
            fail();
        }
        return (handle as raw void*?) as raw Request*?;
    }
}

protected raw Request*? of_accept(raw AcceptHandle*? handle) {
    unsafe {
        if (handle == null) {
            fail();
        }
        return (handle as raw void*?) as raw Request*?;
    }
}

protected raw Request*? of_datagram(raw DatagramHandle*? handle) {
    unsafe {
        if (handle == null) {
            fail();
        }
        return (handle as raw void*?) as raw Request*?;
    }
}

protected raw Request*? of_source(raw Source*? source) {
    unsafe {
        return source->context as raw Request*?;
    }
}

protected raw Request*? of_job(raw Job*? job) {
    unsafe {
        return job->context as raw Request*?;
    }
}

protected c_int error_number() {
    return core::c_errno();
}

protected void close_descriptor(c_int descriptor) {
    unsafe {
        runtime.system::close(descriptor) as void;
    }
}

// The deadline of a timeout counted from now; false beyond the signed horizon of native timers.
protected bool deadline_after(u64 timeout, raw u64* deadline) {
    if (timeout > i64_maximum) {
        return false;
    }
    u64 current = runtime.reactor::now();
    u64 limit = u64_maximum - timeout;
    unsafe {
        if (current > limit) {
            *deadline = u64_maximum;
        } else {
            *deadline = current + timeout;
        }
    }
    return true;
}

// ---- Testing: a pause of the next datagram job before its native call, and a native failure
// of the next one (always present; an unarmed hook costs one atomic load) ----

atomic bool testing_hint = false;
Mutex testing_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
Condition testing_condition = {.signature = runtime.system::condition_signature as c_long,
                               .bytes = {}};
bool testing_pause_next = false;
bool testing_paused = false;
bool testing_release = false;
c_int testing_native_error = 0i32 as c_int;

protected raw Mutex* testing_lock() {
    unsafe {
        raw Mutex* mutex = &testing_mutex as raw Mutex*;
        runtime.system::require_zero(runtime.system::pthread_mutex_lock(mutex));
        return mutex;
    }
}

protected void testing_unlock(raw Mutex* mutex) {
    unsafe {
        runtime.system::require_zero(runtime.system::pthread_mutex_unlock(mutex));
    }
}

protected void testing_wait(raw Mutex* mutex) {
    unsafe {
        runtime.system::require_zero(runtime.system::pthread_cond_wait(
            &testing_condition as raw Condition*, mutex));
    }
}

protected void testing_broadcast() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_cond_broadcast(&testing_condition as raw Condition*));
    }
}

protected void testing_fail(raw Mutex* mutex) {
    testing_unlock(mutex);
    fail();
}

@safety("R-RUNTIME-SOCKET-TESTING-PAUSE", "None")
extern "C" void r_runtime_darwin_socket_testing_pause_next_datagram_before_native() {
    raw Mutex* mutex = testing_lock();
    unsafe {
        if ((testing_pause_next == true) || (testing_paused == true) ||
            (testing_release == true)) {
            testing_fail(mutex);
        }
        testing_pause_next = true;
    }
    core::atomic_store(&testing_hint, true, core::memory_order::release);
    testing_unlock(mutex);
}

@safety("R-RUNTIME-SOCKET-TESTING-WAIT", "a pause is armed")
extern "C" void r_runtime_darwin_socket_testing_wait_for_datagram_before_native() {
    raw Mutex* mutex = testing_lock();
    unsafe {
        while (testing_paused == false) {
            testing_wait(mutex);
        }
    }
    testing_unlock(mutex);
}

@safety("R-RUNTIME-SOCKET-TESTING-RELEASE", "a datagram job waits in its pause")
extern "C" void r_runtime_darwin_socket_testing_release_datagram_before_native() {
    raw Mutex* mutex = testing_lock();
    unsafe {
        if ((testing_paused == false) || (testing_release == true)) {
            testing_fail(mutex);
        }
        testing_release = true;
    }
    testing_broadcast();
    testing_unlock(mutex);
}

@safety("R-RUNTIME-SOCKET-TESTING-FAIL", "native_error is a nonzero errno value")
extern "C" void r_runtime_darwin_socket_testing_fail_next_datagram_native(c_int native_error) {
    if (native_error == 0i32 as c_int) {
        fail();
    }
    raw Mutex* mutex = testing_lock();
    unsafe {
        if (testing_native_error != 0i32 as c_int) {
            testing_fail(mutex);
        }
        testing_native_error = native_error;
    }
    core::atomic_store(&testing_hint, true, core::memory_order::release);
    testing_unlock(mutex);
}

// Runs the armed pause of a datagram job and returns the armed native failure, or zero.
protected c_int testing_before_native() {
    if (core::atomic_load(&testing_hint, core::memory_order::acquire) == false) {
        return 0i32 as c_int;
    }
    raw Mutex* mutex = testing_lock();
    unsafe {
        if (testing_pause_next == true) {
            testing_pause_next = false;
            testing_paused = true;
            testing_broadcast();
            while (testing_release == false) {
                testing_wait(mutex);
            }
            testing_release = false;
            testing_paused = false;
        }
        c_int native_error = testing_native_error;
        testing_native_error = 0i32 as c_int;
        if (testing_pause_next == false) {
            core::atomic_store(&testing_hint, false, core::memory_order::release);
        }
        testing_unlock(mutex);
        return native_error;
    }
}

// ---- The request ----

// Arms the deadline alarm once, at its fixed deadline.
protected void arm_alarm_locked(raw Request*? request) {
    unsafe {
        if ((request->timed == true) && (request->alarm_activated == false)) {
            request->alarm_activated = true;
            runtime.reactor::arm(&request->alarm as raw Source*, request->deadline) as void;
        }
    }
}

// Arms the readiness watch once; the native job arms it again after a spurious wakeup.
protected void arm_ready_locked(raw Request*? request) {
    unsafe {
        if (request->ready_activated == false) {
            request->ready_activated = true;
            runtime.reactor::watch(&request->ready as raw Source*) as void;
        }
    }
}

// Disarms both sources; each settles once, at once when it is not registered.
protected void cancel_sources_locked(raw Request*? request) {
    unsafe {
        runtime.reactor::disarm(&request->ready as raw Source*) as void;
        if (request->timed == true) {
            runtime.reactor::disarm(&request->alarm as raw Source*) as void;
        }
    }
}

// Selects a terminal outcome unless one with an earlier or equal event sequence was selected or
// the completion was delivered; a sent datagram is never replaced. An accepted descriptor that an
// earlier event replaces is closed.
protected bool select_locked(raw Request*? request, TerminalEvent event, u64 sequence,
                             c_int native_error, bool succeeded) {
    if (sequence == 0u64) {
        fail();
    }
    unsafe {
        if (request->completion_delivered == true) {
            return false;
        }
        if (request->terminal_selected == true) {
            bool sent = (request->kind == Kind::datagram) &&
                        (request->operation ==
                         DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND) &&
                        (request->succeeded == true);
            if (sent == true) {
                return false;
            }
            if (sequence >= request->terminal_sequence) {
                return false;
            }
            if ((request->kind == Kind::accept) && (request->succeeded == true)) {
                close_descriptor(request->accepted);
                request->accepted = -1i32 as c_int;
            }
        }
        request->terminal_selected = true;
        request->terminal_event = event;
        request->native_error = native_error;
        request->terminal_sequence = sequence;
        request->succeeded = succeeded;
        request->peer_length = 0u32 as c_uint32;
        request->count = 0u64 as c_size;
        request->truncated = false;
        cancel_sources_locked(request);
        return true;
    }
}

// Whether the completion is due: the outcome is selected, nothing is pending and it was not
// delivered. A request that failed closes the descriptor it owns first.
protected bool deliver_locked(raw Request*? request) {
    unsafe {
        if ((request->terminal_selected == false) || (request->pending != 0u32) ||
            (request->completion_delivered == true)) {
            return false;
        }
        if (request->bound == false) {
            fail();
        }
        if (request->succeeded == false) {
            if ((request->kind == Kind::connect) && ((request->descriptor as i32) >= 0i32)) {
                close_descriptor(request->descriptor);
                request->descriptor = -1i32 as c_int;
            }
            if ((request->kind == Kind::accept) && ((request->accepted as i32) >= 0i32)) {
                close_descriptor(request->accepted);
                request->accepted = -1i32 as c_int;
            }
        }
        request->completion_delivered = true;
        return true;
    }
}

protected void finish_pending(raw Request*? request) {
    unsafe {
        if (request->pending == 0u32) {
            request_unlock(request);
            fail();
        }
        request->pending -= 1u32;
        bool due = deliver_locked(request);
        request_unlock(request);
        if (due == true) {
            runtime.tasks::defer(&request->delivery as raw Job*);
        }
    }
}

@callback
@safety("R-RUNTIME-SOCKET-DELIVERY", "context is the request of the job")
protected extern "C" void delivery_run(raw Job*? job) {
    raw Request*? request = of_job(job);
    unsafe {
        raw void*? context = request->completion_context;
        raw void*? handle = request as raw void*?;
        if (request->kind == Kind::connect) {
            raw fn?(raw ConnectHandle*?, raw void*?) -> void connected =
                request->connect_completion;
            if (connected == null) {
                fail();
            }
            connected(handle as raw ConnectHandle*?, context);
            return;
        }
        if (request->kind == Kind::accept) {
            raw fn?(raw AcceptHandle*?, raw void*?) -> void accepted = request->accept_completion;
            if (accepted == null) {
                fail();
            }
            accepted(handle as raw AcceptHandle*?, context);
            return;
        }
        raw fn?(raw DatagramHandle*?, raw void*?) -> void transferred =
            request->datagram_completion;
        if (transferred == null) {
            fail();
        }
        transferred(handle as raw DatagramHandle*?, context);
    }
}

// A source settled: the reactor calls nothing of it again.
@callback
@safety("R-RUNTIME-SOCKET-SETTLED", "context is the request of the source")
protected extern "C" void source_settled(raw Source*? source) {
    raw Request*? request = of_source(source);
    request_lock(request);
    finish_pending(request);
}

// The descriptor became ready: the native call runs in a job, unless the outcome is selected,
// whose disarm settles the watch.
@callback
@safety("R-RUNTIME-SOCKET-READY", "context is the request of the watch")
protected extern "C" void ready_fired(raw Source*? source) {
    raw Request*? request = of_source(source);
    request_lock(request);
    unsafe {
        if (request->terminal_selected == true) {
            request_unlock(request);
            return;
        }
        request->pending += 1u32;
        request_unlock(request);
        runtime.tasks::defer(&request->native as raw Job*);
    }
}

@callback
@safety("R-RUNTIME-SOCKET-DEADLINE", "context is the request of the alarm")
protected extern "C" void alarm_fired(raw Source*? source) {
    raw Request*? request = of_source(source);
    request_lock(request);
    unsafe {
        u64 sequence = runtime.types::r_runtime_darwin_event_sequence_next() as u64;
        select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT,
                      sequence, 0i32 as c_int, false) as void;
    }
    request_unlock(request);
}

// A nonblocking call that would block, or was interrupted, waits for the next readiness.
protected bool retry(c_int native_error) {
    return (native_error == runtime.system::error_again) ||
           (native_error == runtime.system::error_would_block) ||
           (native_error == runtime.system::error_interrupted);
}

// Sets close-on-exec and nonblocking, and suppresses SIGPIPE of a socket the adapter made or
// accepted; the error of the failing call, or zero.
protected c_int configure(c_int descriptor) {
    unsafe {
        c_int descriptor_flags = runtime.system::fcntl(descriptor,
                                                       runtime.system::get_descriptor_flags);
        if ((descriptor_flags as i32) < 0i32) {
            return error_number();
        }
        c_int status_flags = runtime.system::fcntl(descriptor, runtime.system::get_status_flags);
        if ((status_flags as i32) < 0i32) {
            return error_number();
        }
        if ((runtime.system::fcntl(descriptor, runtime.system::set_descriptor_flags,
                                   descriptor_flags | runtime.system::close_on_exec) as i32) !=
            0i32) {
            return error_number();
        }
        if ((runtime.system::fcntl(descriptor, runtime.system::set_status_flags,
                                   status_flags | runtime.system::nonblocking) as i32) != 0i32) {
            return error_number();
        }
        c_int enabled = 1i32 as c_int;
        raw const c_int* enabled_value = &enabled as raw const c_int*;
        if ((runtime.system::setsockopt(descriptor, runtime.system::socket_level,
                                        runtime.system::option_no_sigpipe,
                                        enabled_value as raw const void*?,
                                        sizeof(c_int) as c_uint32) as i32) != 0i32) {
            return error_number();
        }
        return 0i32 as c_int;
    }
}

protected void connect_native_locked(raw Request*? request) {
    unsafe {
        u64 sequence = runtime.types::r_runtime_darwin_event_sequence_next() as u64;
        c_int native_error = 0i32 as c_int;
        c_uint32 size = sizeof(c_int) as c_uint32;
        raw c_int* value = &native_error as raw c_int*;
        raw c_uint32* length = &size as raw c_uint32*;
        if ((runtime.system::getsockopt(request->descriptor, runtime.system::socket_level,
                                        runtime.system::option_error, value as raw void*?,
                                        length as raw c_uint32*?) as i32) != 0i32) {
            native_error = error_number();
        }
        select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE, sequence,
                      native_error, native_error == 0i32 as c_int) as void;
    }
}

protected void accept_native_locked(raw Request*? request) {
    unsafe {
        u64 sequence = runtime.types::r_runtime_darwin_event_sequence_next() as u64;
        SocketStorage peer = {.length = 0u8 as c_uint8, .family = 0u8 as c_uint8, .head = {},
                              .alignment = 0i64 as c_int64, .tail = {}};
        c_uint32 peer_length = sizeof(SocketStorage) as c_uint32;
        raw SocketStorage* peer_storage = &peer as raw SocketStorage*;
        raw void* peer_bytes = peer_storage as raw void*;
        raw c_uint32* length = &peer_length as raw c_uint32*;
        c_int taken = runtime.system::accept(request->descriptor,
                                             peer_bytes as raw SocketAddress*?,
                                             length as raw c_uint32*?);
        if ((taken as i32) < 0i32) {
            c_int accept_error = error_number();
            if (retry(accept_error) == true) {
                sequence as void;
                runtime.reactor::watch(&request->ready as raw Source*) as void;
                return;
            }
            select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, accept_error, false) as void;
            return;
        }
        c_int configure_error = configure(taken);
        if (configure_error != 0i32 as c_int) {
            close_descriptor(taken);
            select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, configure_error, false) as void;
            return;
        }
        if (select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, 0i32 as c_int, true) == false) {
            close_descriptor(taken);
            return;
        }
        request->accepted = taken;
        request->peer = peer;
        request->peer_length = peer_length;
    }
}

protected void datagram_native_locked(raw Request*? request, c_int armed_error) {
    unsafe {
        u64 sequence = runtime.types::r_runtime_darwin_event_sequence_next() as u64;
        bool sending = request->operation ==
                       DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND;
        SocketStorage peer = {.length = 0u8 as c_uint8, .family = 0u8 as c_uint8, .head = {},
                              .alignment = 0i64 as c_int64, .tail = {}};
        c_uint32 peer_length = 0u32 as c_uint32;
        i64 transferred = -1i64;
        bool truncated = false;
        if (armed_error == 0i32 as c_int) {
            if (sending == true) {
                raw const SocketAddress*? target = null;
                if ((request->address_length as u32) != 0u32) {
                    raw SocketStorage* address = &request->address as raw SocketStorage*;
                    raw const void* address_bytes = address as raw const void*;
                    target = address_bytes as raw const SocketAddress*?;
                }
                transferred = runtime.system::sendto(request->descriptor, request->send_data,
                                                     request->size, 0i32 as c_int, target,
                                                     request->address_length) as i64;
            } else {
                // Darwin marks even an empty datagram truncated for a zero-sized vector, so a
                // receive of capacity zero reads into one scratch byte; the count stays zero.
                c_uint8 probe = 0u8 as c_uint8;
                raw c_uint8* probe_byte = &probe as raw c_uint8*;
                IoVector vector = {.base = request->receive_data, .length = request->size};
                if ((request->size as u64) == 0u64) {
                    vector.base = probe_byte as raw void*?;
                    vector.length = 1u64 as c_size;
                }
                raw SocketStorage* peer_storage = &peer as raw SocketStorage*;
                raw IoVector* vectors = &vector as raw IoVector*;
                MessageHeader message = {.name = peer_storage as raw void*?,
                                         .name_length = sizeof(SocketStorage) as c_uint32,
                                         .vector = vectors as raw IoVector*?,
                                         .vector_length = 1i32 as c_int, .control = null,
                                         .control_length = 0u32 as c_uint32,
                                         .flags = 0i32 as c_int};
                transferred = runtime.system::recvmsg(request->descriptor,
                                                      &message as raw MessageHeader*,
                                                      0i32 as c_int) as i64;
                peer_length = message.name_length;
                truncated = (message.flags & runtime.system::message_truncated) !=
                            0i32 as c_int;
            }
        }
        if (transferred < 0i64) {
            c_int failure = armed_error;
            if (armed_error == 0i32 as c_int) {
                failure = error_number();
            }
            if (retry(failure) == true) {
                sequence as void;
                runtime.reactor::watch(&request->ready as raw Source*) as void;
                return;
            }
            select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, failure, false) as void;
            return;
        }
        u64 size = request->size as u64;
        if ((sending == true) && ((transferred as u64) != size)) {
            select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, runtime.system::error_message_size, false) as void;
            return;
        }
        u64 count = size;
        bool beyond = (transferred as u64) > size;
        if ((sending == false) && (beyond == false)) {
            count = transferred as u64;
        }
        if (select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, 0i32 as c_int, true) == false) {
            return;
        }
        request->count = count as c_size;
        request->truncated = (truncated == true) || (beyond == true);
        if (sending == false) {
            request->peer = peer;
            request->peer_length = peer_length;
        }
    }
}

// The native call of a ready descriptor, on a worker. The datagram pause of the tests runs
// before the call without the lock, so a cancellation or deadline may select the outcome
// meanwhile; then no call is made.
@callback
@safety("R-RUNTIME-SOCKET-NATIVE", "context is the request of the job")
protected extern "C" void native_run(raw Job*? job) {
    raw Request*? request = of_job(job);
    unsafe {
        if (request->kind == Kind::datagram) {
            request_lock(request);
            bool selected = request->terminal_selected;
            request_unlock(request);
            c_int armed_error = 0i32 as c_int;
            if (selected == false) {
                armed_error = testing_before_native();
            }
            request_lock(request);
            if (request->terminal_selected == false) {
                datagram_native_locked(request, armed_error);
            } else {
                armed_error as void;
            }
            finish_pending(request);
            return;
        }
        request_lock(request);
        if (request->terminal_selected == false) {
            if (request->kind == Kind::connect) {
                connect_native_locked(request);
            }
            if (request->kind == Kind::accept) {
                accept_native_locked(request);
            }
        }
    }
    finish_pending(request);
}

// Allocates a request with its jobs and deadline alarm; null when the allocator refuses.
protected raw Request*? request_create(raw Allocator*? allocator, Kind kind, u64 timeout) {
    unsafe {
        raw void*? storage = null;
        if (runtime.heap::r_runtime_allocator_allocate(allocator, sizeof(Request) as c_size,
                                                       alignof(Request) as c_size,
                                                       &storage as raw (raw void*?)*) !=
            AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
            return null;
        }
        raw Request*? request = storage as raw Request*?;
        *request = Request {.allocator = allocator, .kind = kind};
        if (runtime.system::pthread_mutex_init(&request->mutex as raw Mutex*, null) !=
            0i32 as c_int) {
            runtime.heap::r_runtime_allocator_deallocate(storage, alignof(Request) as c_size);
            return null;
        }
        request->native = Job {.next = null, .run = native_run, .context = request as raw void*?};
        request->delivery = Job {.next = null, .run = delivery_run,
                                 .context = request as raw void*?};
        request->pending = 1u32;
        if (timeout != 0u64) {
            u64 deadline = 0u64;
            deadline_after(timeout, &deadline as raw u64*) as void;
            request->deadline = deadline;
            request->timed = true;
            runtime.reactor::initialize(&request->alarm as raw Source*, alarm_fired,
                                        source_settled, request as raw void*?);
            request->pending = 2u32;
        }
        return request;
    }
}

// Attaches the descriptor and its readiness watch.
protected void request_watch(raw Request*? request, c_int descriptor, c_int filter) {
    unsafe {
        request->descriptor = descriptor;
        runtime.reactor::initialize_watch(&request->ready as raw Source*, descriptor, filter,
                                          ready_fired, source_settled, request as raw void*?);
    }
}

// Frees a request that never reached the reactor or whose completion was delivered.
protected void request_destroy(raw Request*? request) {
    unsafe {
        if (request->receive_data != null) {
            runtime.heap::r_runtime_allocator_deallocate(request->receive_data, 1u64 as c_size);
            request->receive_data = null;
        }
        runtime.system::require_zero(
            runtime.system::pthread_mutex_destroy(&request->mutex as raw Mutex*));
        runtime.heap::r_runtime_allocator_deallocate(request as raw void*?,
                                                     alignof(Request) as c_size);
    }
}

// Binds the completion of a request that was not activated nor selected.
protected bool bind_locked(raw Request*? request) {
    unsafe {
        return (request->ready_activated == false) && (request->alarm_activated == false) &&
               (request->terminal_selected == false) && (request->bound == false) &&
               ((request->descriptor as i32) >= 0i32);
    }
}

// Starts the reactor and the worker pool, which the adapter's sources and jobs need.
protected bool start_runtime() {
    return (runtime.reactor::start() == true) && (runtime.tasks::pool_start() == true);
}

// ---- Connect ----

protected ConnectPrepared connect_prepared(raw Request*? request, PrepareStatus status,
                                           c_int native_error) {
    unsafe {
        return ConnectPrepared {.request = (request as raw void*?) as raw ConnectHandle*?,
                                .status = status, .native_error = native_error};
    }
}

protected bool remote_valid(raw const SocketAddress* remote, c_uint32 remote_length) {
    unsafe {
        c_int family = remote->family as c_int;
        u32 length = remote_length as u32;
        if (family == runtime.system::family_inet) {
            return length == inet_length;
        }
        if (family == runtime.system::family_inet6) {
            return length == inet6_length;
        }
        if (family == runtime.system::family_local) {
            return (length > 2u32) && (length <= sizeof(LocalAddress) as u32);
        }
        return false;
    }
}

@safety("R-RUNTIME-SOCKET-CONNECT-PREPARE", "remote addresses remote_length bytes")
extern "C" ConnectPrepared r_runtime_darwin_socket_connect_prepare(
    raw Allocator*? allocator, raw const SocketAddress* remote, c_uint32 remote_length,
    c_uint64 timeout_nanoseconds) {
    unsafe {
        u64 timeout = timeout_nanoseconds as u64;
        if ((remote_valid(remote, remote_length) == false) || (timeout > i64_maximum) ||
            ((remote_length as u64) > sizeof(SocketStorage) as u64)) {
            return connect_prepared(null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID,
                                    0i32 as c_int);
        }
        if (start_runtime() == false) {
            return connect_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
                runtime.system::error_no_memory);
        }
        // The request is allocated before the socket, so a refused allocation makes no socket.
        raw Request*? request = request_create(allocator, Kind::connect, timeout);
        if (request == null) {
            return connect_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
                runtime.system::error_no_memory);
        }
        c_int descriptor = runtime.system::socket(remote->family as c_int,
                                                  runtime.system::socket_stream, 0i32 as c_int);
        if ((descriptor as i32) < 0i32) {
            c_int socket_error = error_number();
            request_destroy(request);
            return connect_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, socket_error);
        }
        c_int configure_error = configure(descriptor);
        if (configure_error != 0i32 as c_int) {
            close_descriptor(descriptor);
            request_destroy(request);
            return connect_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED,
                configure_error);
        }
        request_watch(request, descriptor, runtime.system::filter_write);
        raw SocketStorage* address = &request->address as raw SocketStorage*;
        runtime.types::memcpy(address as raw void*?, remote as raw const void*?,
                              remote_length as c_size) as void;
        request->address_length = remote_length;
        return connect_prepared(request, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_OK,
                                0i32 as c_int);
    }
}

@safety("R-RUNTIME-SOCKET-CONNECT-BIND", "request is a prepared connect")
extern "C" c_bool r_runtime_darwin_socket_connect_bind(
    raw ConnectHandle*? handle, raw fn?(raw ConnectHandle*?, raw void*?) -> void completion,
    raw void*? context) {
    raw Request*? request = of_connect(handle);
    request_lock(request);
    unsafe {
        bool bound = bind_locked(request);
        if (bound == true) {
            request->connect_completion = completion;
            request->completion_context = context;
            request->bound = true;
        }
        request_unlock(request);
        return runtime.types::c_bool_of(bound);
    }
}

@safety("R-RUNTIME-SOCKET-CONNECT-ACTIVATE", "request is a bound connect")
extern "C" c_bool r_runtime_darwin_socket_connect_activate(raw ConnectHandle*? handle) {
    raw Request*? request = of_connect(handle);
    request_lock(request);
    unsafe {
        if ((request->ready_activated == true) || (request->terminal_selected == true) ||
            (request->bound == false) || ((request->descriptor as i32) < 0i32)) {
            request_unlock(request);
            return runtime.types::c_bool_of(false);
        }
        raw SocketStorage* address = &request->address as raw SocketStorage*;
        raw const void* address_bytes = address as raw const void*;
        c_int status = runtime.system::connect(request->descriptor,
                                               address_bytes as raw const SocketAddress*,
                                               request->address_length);
        c_int native_error = 0i32 as c_int;
        if ((status as i32) != 0i32) {
            native_error = error_number();
        }
        bool connected = (native_error == 0i32 as c_int) ||
                         (native_error == runtime.system::error_connected);
        if (connected == true) {
            u64 sequence = runtime.types::r_runtime_darwin_event_sequence_next() as u64;
            select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                          sequence, 0i32 as c_int, true) as void;
        } else {
            bool waiting = (native_error == runtime.system::error_in_progress) ||
                           (native_error == runtime.system::error_already) ||
                           (native_error == runtime.system::error_interrupted);
            if (waiting == true) {
                arm_ready_locked(request);
                arm_alarm_locked(request);
            } else {
                u64 sequence = runtime.types::r_runtime_darwin_event_sequence_next() as u64;
                select_locked(request, TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                              sequence, native_error, false) as void;
            }
        }
        request->ready_activated = true;
        request_unlock(request);
        return runtime.types::c_bool_of(true);
    }
}

@safety("R-RUNTIME-SOCKET-CONNECT-CANCEL", "request is a live connect")
extern "C" c_bool r_runtime_darwin_socket_connect_cancel(raw ConnectHandle*? handle,
                                                         c_uint64 cancellation_sequence) {
    raw Request*? request = of_connect(handle);
    request_lock(request);
    bool selected = select_locked(request,
                                  TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED,
                                  cancellation_sequence as u64, 0i32 as c_int, false);
    request_unlock(request);
    return runtime.types::c_bool_of(selected);
}

@safety("R-RUNTIME-SOCKET-CONNECT-RESULT", "request is a live connect")
extern "C" ConnectResult r_runtime_darwin_socket_connect_result(raw ConnectHandle*? handle) {
    raw Request*? request = of_connect(handle);
    request_lock(request);
    unsafe {
        ConnectResult result = {.terminal_event =
                                    TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                .native_error = 0i32 as c_int,
                                .terminal_event_sequence = 0u64 as c_uint64,
                                .connected = runtime.types::c_bool_of(false)};
        if (request->completion_delivered == true) {
            result.terminal_event = request->terminal_event;
            result.native_error = request->native_error;
            result.terminal_event_sequence = request->terminal_sequence as c_uint64;
            result.connected = runtime.types::c_bool_of(request->succeeded);
        }
        request_unlock(request);
        return result;
    }
}

@safety("R-RUNTIME-SOCKET-CONNECT-TAKE", "request is a live connect")
extern "C" c_int r_runtime_darwin_socket_connect_take_descriptor(raw ConnectHandle*? handle) {
    raw Request*? request = of_connect(handle);
    request_lock(request);
    unsafe {
        c_int descriptor = -1i32 as c_int;
        if ((request->completion_delivered == true) && (request->succeeded == true) &&
            (request->descriptor_taken == false) && ((request->descriptor as i32) >= 0i32)) {
            descriptor = request->descriptor;
            request->descriptor = -1i32 as c_int;
            request->descriptor_taken = true;
        }
        request_unlock(request);
        return descriptor;
    }
}

@safety("R-RUNTIME-SOCKET-CONNECT-RELEASE", "the completion of request was delivered")
extern "C" void r_runtime_darwin_socket_connect_release(raw ConnectHandle*? handle) {
    raw Request*? request = of_connect(handle);
    request_lock(request);
    unsafe {
        if ((request->completion_delivered == false) || (request->pending != 0u32)) {
            request_unlock(request);
            fail();
        }
        if ((request->descriptor as i32) >= 0i32) {
            close_descriptor(request->descriptor);
            request->descriptor = -1i32 as c_int;
        }
    }
    request_unlock(request);
    request_destroy(request);
}

@safety("R-RUNTIME-SOCKET-CONNECT-ABORT", "slot is null or holds a prepared connect")
extern "C" void r_runtime_darwin_socket_connect_abort(raw (raw ConnectHandle*?)*? slot) {
    unsafe {
        if ((slot == null) || (*slot == null)) {
            return;
        }
        raw Request*? request = of_connect(*slot);
        *slot = null;
        request_lock(request);
        if ((request->ready_activated == true) || (request->terminal_selected == true) ||
            (request->bound == true)) {
            request_unlock(request);
            fail();
        }
        request_unlock(request);
        if ((request->descriptor as i32) >= 0i32) {
            close_descriptor(request->descriptor);
        }
        request_destroy(request);
    }
}

// ---- Accept ----

protected AcceptPrepared accept_prepared(raw Request*? request, PrepareStatus status,
                                         c_int native_error) {
    unsafe {
        return AcceptPrepared {.request = (request as raw void*?) as raw AcceptHandle*?,
                               .status = status, .native_error = native_error};
    }
}

// Checks that a borrowed socket is close-on-exec, nonblocking and of the kind; the error of the
// failing call, -1 for a socket of another shape, or zero.
protected c_int borrowed_shape(c_int descriptor, c_int kind) {
    unsafe {
        c_int descriptor_flags = runtime.system::fcntl(descriptor,
                                                       runtime.system::get_descriptor_flags);
        if ((descriptor_flags as i32) < 0i32) {
            return error_number();
        }
        c_int status_flags = runtime.system::fcntl(descriptor, runtime.system::get_status_flags);
        if ((status_flags as i32) < 0i32) {
            return error_number();
        }
        c_int socket_kind = 0i32 as c_int;
        c_uint32 size = sizeof(c_int) as c_uint32;
        raw c_int* value = &socket_kind as raw c_int*;
        raw c_uint32* length = &size as raw c_uint32*;
        if ((runtime.system::getsockopt(descriptor, runtime.system::socket_level,
                                        runtime.system::option_type, value as raw void*?,
                                        length as raw c_uint32*?) as i32) != 0i32) {
            return error_number();
        }
        bool executable = (descriptor_flags & runtime.system::close_on_exec) == 0i32 as c_int;
        bool blocking = (status_flags & runtime.system::nonblocking) == 0i32 as c_int;
        if ((executable == true) || (blocking == true) || (socket_kind != kind)) {
            return -1i32 as c_int;
        }
        return 0i32 as c_int;
    }
}

@safety("R-RUNTIME-SOCKET-ACCEPT-PREPARE", "None")
extern "C" AcceptPrepared r_runtime_darwin_socket_accept_prepare(raw Allocator*? allocator,
                                                                 c_int listener_descriptor,
                                                                 c_uint64 timeout_nanoseconds) {
    u64 timeout = timeout_nanoseconds as u64;
    if (((listener_descriptor as i32) < 0i32) || (timeout > i64_maximum)) {
        return accept_prepared(null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID,
                               0i32 as c_int);
    }
    c_int shape = borrowed_shape(listener_descriptor, runtime.system::socket_stream);
    if ((shape as i32) < 0i32) {
        return accept_prepared(null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID,
                               0i32 as c_int);
    }
    if (shape != 0i32 as c_int) {
        return accept_prepared(null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED,
                               shape);
    }
    if (start_runtime() == false) {
        return accept_prepared(
            null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
            runtime.system::error_no_memory);
    }
    raw Request*? request = request_create(allocator, Kind::accept, timeout);
    unsafe {
        if (request == null) {
            return accept_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
                runtime.system::error_no_memory);
        }
    }
    request_watch(request, listener_descriptor, runtime.system::filter_read);
    return accept_prepared(request, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_OK,
                           0i32 as c_int);
}

@safety("R-RUNTIME-SOCKET-ACCEPT-BIND", "request is a prepared accept")
extern "C" c_bool r_runtime_darwin_socket_accept_bind(
    raw AcceptHandle*? handle, raw fn?(raw AcceptHandle*?, raw void*?) -> void completion,
    raw void*? context) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    unsafe {
        bool bound = bind_locked(request);
        if (bound == true) {
            request->accept_completion = completion;
            request->completion_context = context;
            request->bound = true;
        }
        request_unlock(request);
        return runtime.types::c_bool_of(bound);
    }
}

@safety("R-RUNTIME-SOCKET-ACCEPT-DEADLINE", "request is a bound accept")
extern "C" c_bool r_runtime_darwin_socket_accept_arm_deadline(raw AcceptHandle*? handle) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    unsafe {
        if ((request->terminal_selected == true) || (request->bound == false)) {
            request_unlock(request);
            return runtime.types::c_bool_of(false);
        }
        arm_alarm_locked(request);
    }
    request_unlock(request);
    return runtime.types::c_bool_of(true);
}

@safety("R-RUNTIME-SOCKET-ACCEPT-ACTIVATE", "request is a bound accept")
extern "C" c_bool r_runtime_darwin_socket_accept_activate(raw AcceptHandle*? handle) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    unsafe {
        if ((request->terminal_selected == true) || (request->ready_activated == true)) {
            request_unlock(request);
            return runtime.types::c_bool_of(true);
        }
        if (request->bound == false) {
            request_unlock(request);
            return runtime.types::c_bool_of(false);
        }
        arm_alarm_locked(request);
        arm_ready_locked(request);
    }
    request_unlock(request);
    return runtime.types::c_bool_of(true);
}

@safety("R-RUNTIME-SOCKET-ACCEPT-CANCEL", "request is a live accept")
extern "C" c_bool r_runtime_darwin_socket_accept_cancel(raw AcceptHandle*? handle,
                                                        c_uint64 cancellation_sequence) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    bool selected = select_locked(request,
                                  TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED,
                                  cancellation_sequence as u64, 0i32 as c_int, false);
    request_unlock(request);
    return runtime.types::c_bool_of(selected);
}

@safety("R-RUNTIME-SOCKET-ACCEPT-RESULT", "request is a live accept")
extern "C" AcceptResult r_runtime_darwin_socket_accept_result(raw AcceptHandle*? handle) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    unsafe {
        AcceptResult result = {.terminal_event =
                                   TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                               .native_error = 0i32 as c_int,
                               .terminal_event_sequence = 0u64 as c_uint64,
                               .peer = {.length = 0u8 as c_uint8, .family = 0u8 as c_uint8,
                                        .head = {}, .alignment = 0i64 as c_int64, .tail = {}},
                               .peer_length = 0u32 as c_uint32,
                               .accepted = runtime.types::c_bool_of(false)};
        if (request->completion_delivered == true) {
            result.terminal_event = request->terminal_event;
            result.native_error = request->native_error;
            result.terminal_event_sequence = request->terminal_sequence as c_uint64;
            result.accepted = runtime.types::c_bool_of(request->succeeded);
            if (request->succeeded == true) {
                result.peer = request->peer;
                result.peer_length = request->peer_length;
            }
        }
        request_unlock(request);
        return result;
    }
}

@safety("R-RUNTIME-SOCKET-ACCEPT-TAKE", "request is a live accept")
extern "C" c_int r_runtime_darwin_socket_accept_take_descriptor(raw AcceptHandle*? handle) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    unsafe {
        c_int descriptor = -1i32 as c_int;
        if ((request->completion_delivered == true) && (request->succeeded == true) &&
            (request->descriptor_taken == false) && ((request->accepted as i32) >= 0i32)) {
            descriptor = request->accepted;
            request->accepted = -1i32 as c_int;
            request->descriptor_taken = true;
        }
        request_unlock(request);
        return descriptor;
    }
}

@safety("R-RUNTIME-SOCKET-ACCEPT-RELEASE", "the completion of request was delivered")
extern "C" void r_runtime_darwin_socket_accept_release(raw AcceptHandle*? handle) {
    raw Request*? request = of_accept(handle);
    request_lock(request);
    unsafe {
        if ((request->completion_delivered == false) || (request->pending != 0u32)) {
            request_unlock(request);
            fail();
        }
        if ((request->accepted as i32) >= 0i32) {
            close_descriptor(request->accepted);
            request->accepted = -1i32 as c_int;
        }
    }
    request_unlock(request);
    request_destroy(request);
}

@safety("R-RUNTIME-SOCKET-ACCEPT-ABORT", "slot is null or holds a prepared accept")
extern "C" void r_runtime_darwin_socket_accept_abort(raw (raw AcceptHandle*?)*? slot) {
    unsafe {
        if ((slot == null) || (*slot == null)) {
            return;
        }
        raw Request*? request = of_accept(*slot);
        *slot = null;
        request_lock(request);
        if ((request->ready_activated == true) || (request->alarm_activated == true) ||
            (request->terminal_selected == true) || (request->bound == true)) {
            request_unlock(request);
            fail();
        }
        request_unlock(request);
        request_destroy(request);
    }
}

// ---- Datagrams ----

protected DatagramPrepared datagram_prepared(raw Request*? request, PrepareStatus status,
                                             c_int native_error) {
    unsafe {
        return DatagramPrepared {.request = (request as raw void*?) as raw DatagramHandle*?,
                                 .status = status, .native_error = native_error};
    }
}

protected DatagramPrepared datagram_prepare(raw Allocator*? allocator, c_int descriptor,
                                            DatagramOperation operation,
                                            raw const SocketAddress*? peer, c_uint32 peer_length,
                                            raw const void*? data, c_size size, u64 timeout) {
    unsafe {
        bool sending = operation == DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND;
        bool peer_invalid = false;
        if (sending == true) {
            if (peer == null) {
                peer_invalid = (peer_length as u32) != 0u32;
            } else {
                peer_invalid = ((peer_length as u32) == 0u32) ||
                               ((peer_length as u64) > sizeof(SocketStorage) as u64);
            }
        }
        if (((descriptor as i32) < 0i32) || (timeout > i64_maximum) || (peer_invalid == true)) {
            return datagram_prepared(null,
                                     PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID,
                                     0i32 as c_int);
        }
        c_int shape = borrowed_shape(descriptor, runtime.system::socket_datagram);
        if ((shape as i32) < 0i32) {
            return datagram_prepared(null,
                                     PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID,
                                     0i32 as c_int);
        }
        if (shape != 0i32 as c_int) {
            return datagram_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, shape);
        }
        if (start_runtime() == false) {
            return datagram_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
                runtime.system::error_no_memory);
        }
        c_int filter = runtime.system::filter_read;
        if (sending == true) {
            filter = runtime.system::filter_write;
        }
        raw Request*? request = request_create(allocator, Kind::datagram, timeout);
        if (request == null) {
            return datagram_prepared(
                null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
                runtime.system::error_no_memory);
        }
        request_watch(request, descriptor, filter);
        request->operation = operation;
        request->size = size;
        if (sending == true) {
            request->send_data = data;
            if (peer != null) {
                raw SocketStorage* address = &request->address as raw SocketStorage*;
                runtime.types::memcpy(address as raw void*?, peer as raw const void*?,
                                      peer_length as c_size) as void;
            }
            request->address_length = peer_length;
        } else {
            // Independent bounce storage: a failed or cancelled receive leaves the caller's
            // buffer as it was.
            u64 capacity = size as u64;
            if (capacity == 0u64) {
                capacity = 1u64;
            }
            raw void*? storage = null;
            if (runtime.heap::r_runtime_allocator_allocate(allocator, capacity as c_size,
                                                           1u64 as c_size,
                                                           &storage as raw (raw void*?)*) !=
                AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
                request_destroy(request);
                return datagram_prepared(
                    null, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
                    runtime.system::error_no_memory);
            }
            request->receive_data = storage;
        }
        return datagram_prepared(request, PrepareStatus::R_RUNTIME_DARWIN_SOCKET_PREPARE_OK,
                                 0i32 as c_int);
    }
}

@safety("R-RUNTIME-SOCKET-SEND-PREPARE",
        "peer addresses peer_length bytes or is null; data addresses size bytes until completion")
extern "C" DatagramPrepared r_runtime_darwin_socket_datagram_send_prepare(
    raw Allocator*? allocator, c_int descriptor, raw const SocketAddress*? peer,
    c_uint32 peer_length, raw const void*? data, c_size size, c_uint64 timeout_nanoseconds) {
    return datagram_prepare(allocator, descriptor,
                            DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND, peer,
                            peer_length, data, size, timeout_nanoseconds as u64);
}

@safety("R-RUNTIME-SOCKET-RECEIVE-PREPARE", "None")
extern "C" DatagramPrepared r_runtime_darwin_socket_datagram_receive_prepare(
    raw Allocator*? allocator, c_int descriptor, c_size capacity, c_uint64 timeout_nanoseconds) {
    return datagram_prepare(allocator, descriptor,
                            DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE, null,
                            0u32 as c_uint32, null, capacity, timeout_nanoseconds as u64);
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-BIND", "request is a prepared datagram transfer")
extern "C" c_bool r_runtime_darwin_socket_datagram_bind(
    raw DatagramHandle*? handle, raw fn?(raw DatagramHandle*?, raw void*?) -> void completion,
    raw void*? context) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    unsafe {
        bool bound = bind_locked(request);
        if (bound == true) {
            request->datagram_completion = completion;
            request->completion_context = context;
            request->bound = true;
        }
        request_unlock(request);
        return runtime.types::c_bool_of(bound);
    }
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-DEADLINE", "request is a bound datagram transfer")
extern "C" c_bool r_runtime_darwin_socket_datagram_arm_deadline(raw DatagramHandle*? handle) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    unsafe {
        if ((request->terminal_selected == true) || (request->bound == false)) {
            request_unlock(request);
            return runtime.types::c_bool_of(false);
        }
        arm_alarm_locked(request);
    }
    request_unlock(request);
    return runtime.types::c_bool_of(true);
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-ACTIVATE", "request is a bound datagram transfer")
extern "C" c_bool r_runtime_darwin_socket_datagram_activate(raw DatagramHandle*? handle) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    unsafe {
        if ((request->terminal_selected == true) || (request->ready_activated == true)) {
            request_unlock(request);
            return runtime.types::c_bool_of(true);
        }
        if (request->bound == false) {
            request_unlock(request);
            return runtime.types::c_bool_of(false);
        }
        arm_alarm_locked(request);
        arm_ready_locked(request);
    }
    request_unlock(request);
    return runtime.types::c_bool_of(true);
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-CANCEL", "request is a live datagram transfer")
extern "C" c_bool r_runtime_darwin_socket_datagram_cancel(raw DatagramHandle*? handle,
                                                          c_uint64 cancellation_sequence) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    bool selected = select_locked(request,
                                  TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED,
                                  cancellation_sequence as u64, 0i32 as c_int, false);
    request_unlock(request);
    return runtime.types::c_bool_of(selected);
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-RESULT", "request is a live datagram transfer")
extern "C" DatagramResult r_runtime_darwin_socket_datagram_result(raw DatagramHandle*? handle) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    unsafe {
        DatagramResult result = {.operation =
                                     DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND,
                                 .terminal_event =
                                     TerminalEvent::R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                 .native_error = 0i32 as c_int,
                                 .terminal_event_sequence = 0u64 as c_uint64,
                                 .peer = {.length = 0u8 as c_uint8, .family = 0u8 as c_uint8,
                                          .head = {}, .alignment = 0i64 as c_int64,
                                          .tail = {}},
                                 .peer_length = 0u32 as c_uint32, .count = 0u64 as c_size,
                                 .completed = runtime.types::c_bool_of(false),
                                 .truncated = runtime.types::c_bool_of(false)};
        if (request->completion_delivered == true) {
            result.operation = request->operation;
            result.terminal_event = request->terminal_event;
            result.native_error = request->native_error;
            result.terminal_event_sequence = request->terminal_sequence as c_uint64;
            result.count = request->count;
            result.completed = runtime.types::c_bool_of(request->succeeded);
            result.truncated = runtime.types::c_bool_of(request->truncated);
            bool received = (request->succeeded == true) &&
                            (request->operation ==
                             DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE);
            if (received == true) {
                result.peer = request->peer;
                result.peer_length = request->peer_length;
            }
        }
        request_unlock(request);
        return result;
    }
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-COPY", "destination addresses capacity writable bytes")
extern "C" c_bool r_runtime_darwin_socket_datagram_copy_received(raw DatagramHandle*? handle,
                                                                 raw void*? destination,
                                                                 c_size capacity) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    unsafe {
        u64 count = request->count as u64;
        bool received = (request->completion_delivered == true) &&
                        (request->operation ==
                         DatagramOperation::R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE) &&
                        (request->succeeded == true);
        bool fits = (count <= (capacity as u64)) && ((count == 0u64) || (destination != null));
        bool copied = (received == true) && (fits == true);
        if ((copied == true) && (count != 0u64)) {
            runtime.types::memcpy(destination, request->receive_data as raw const void*?,
                                  count as c_size) as void;
        }
        request_unlock(request);
        return runtime.types::c_bool_of(copied);
    }
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-RELEASE", "the completion of request was delivered")
extern "C" void r_runtime_darwin_socket_datagram_release(raw DatagramHandle*? handle) {
    raw Request*? request = of_datagram(handle);
    request_lock(request);
    unsafe {
        if ((request->completion_delivered == false) || (request->pending != 0u32)) {
            request_unlock(request);
            fail();
        }
    }
    request_unlock(request);
    request_destroy(request);
}

@safety("R-RUNTIME-SOCKET-DATAGRAM-ABORT", "slot is null or holds a prepared datagram transfer")
extern "C" void r_runtime_darwin_socket_datagram_abort(raw (raw DatagramHandle*?)*? slot) {
    unsafe {
        if ((slot == null) || (*slot == null)) {
            return;
        }
        raw Request*? request = of_datagram(*slot);
        *slot = null;
        request_lock(request);
        if ((request->ready_activated == true) || (request->alarm_activated == true) ||
            (request->terminal_selected == true) || (request->bound == true)) {
            request_unlock(request);
            fail();
        }
        request_unlock(request);
        request_destroy(request);
    }
}
