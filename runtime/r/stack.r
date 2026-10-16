module runtime.stacks;
import runtime.types;
import runtime.types::{SourceSpan};
import runtime.system;
import runtime.panics;

// The stack bounds of each thread (r_runtime_core.h, R-FUNC-0004): an R entry requires the
// static bound of the code it enters below the stack pointer, above a protected low area.
@header("r_runtime_core.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_target_abi.h")
extern "C" {
    @c_constant(name = "R_RUNTIME_STACK_PROTECTED_LOW_BYTES")
    const c_size protected_low_bytes = 65536u64 as c_size;
    @c_constant(name = "R_RUNTIME_STACK_CALL_TRANSITION_BYTES")
    const c_size call_transition_bytes = 16384u64 as c_size;
}

struct Bounds {
    usize protected_low = 0usize;
    usize high = 0usize;
    bool initialized = false;
};

thread_local Bounds bounds = {};

// Whether frame bytes and one call transition fit between the stack pointer and the protected
// low area of this thread.
protected bool fits(usize frame) {
    if (bounds.initialized == false) {
        return false;
    }
    usize transition = call_transition_bytes as usize;
    if (frame > runtime.types::size_maximum - transition) {
        return false;
    }
    usize required = frame + transition;
    usize current = core::stack_pointer();
    return (current <= bounds.high) && (current > bounds.protected_low) &&
           (required <= current - bounds.protected_low);
}

@safety("R-RUNTIME-STACK-INITIALIZE", "None")
extern "C" c_bool r_runtime_stack_initialize_current_thread() {
    bounds.initialized = false;
    unsafe {
        raw runtime.system::Thread* self = runtime.system::pthread_self();
        usize high = core::pointer_address(runtime.system::pthread_get_stackaddr_np(self));
        usize size = runtime.system::pthread_get_stacksize_np(self) as usize;
        usize reserve = protected_low_bytes as usize;
        bool wrong = (size == 0usize) || (size > high);
        if ((high == 0usize) || (wrong == true)) {
            return 0i32 as c_bool;
        }
        usize low = high - size;
        if (low > runtime.types::size_maximum - reserve) {
            return 0i32 as c_bool;
        }
        usize protected_low = low + reserve;
        usize current = core::stack_pointer();
        bool outside = (current <= protected_low) || (current > high);
        if ((protected_low >= high) || (outside == true)) {
            return 0i32 as c_bool;
        }
        bounds = Bounds {.protected_low = protected_low, .high = high, .initialized = true};
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-STACK-REQUIRE", "None")
extern "C" void r_runtime_stack_require(c_size frame_bytes, SourceSpan span) {
    if (fits(frame_bytes as usize) == false) {
        runtime.panics::r_runtime_panic(12u32 as c_uint, span);
    }
}

@safety("R-RUNTIME-STACK-CAN-REQUIRE", "None")
extern "C" c_bool r_runtime_stack_can_require(c_size frame_bytes) {
    if (fits(frame_bytes as usize) == true) {
        return 1i32 as c_bool;
    }
    return 0i32 as c_bool;
}
