module test.codegen.ffi_never_results;

/* R-TYPE-0007, R-FFI-0021: a C import, a callback and a raw C function pointer with a never
   result. The C function ends the process; the callback ends every path in a never call; a never
   import call is a terminating statement and converts to the type its context requires. */

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-EXIT", "The process ends with the status")
    never probe_exit(c_int status);

    @safety("PROBE-CALL-FATAL", "fatal shall be a valid C function pointer that does not return")
    c_int probe_call_fatal(raw fn(c_int) -> never fatal, c_int code);
}

@callback
@safety("PROBE-FATAL", "The runtime is initialized")
extern "C" never probe_fatal(c_int code) {
    unsafe {
        probe_exit(code);
    }
}

/* A never import call as an initializer and as a return operand. */
i32 checked(i32 value) {
    if (value < 0) {
        unsafe {
            i32 stopped = probe_exit(3i32 as c_int);
            return stopped;
        }
    }
    if (value > 100) {
        unsafe {
            return probe_exit(4i32 as c_int);
        }
    }
    return value;
}

/* An R call through a never raw C function pointer. */
never fire(raw fn(c_int) -> never fatal, i32 code) {
    unsafe {
        fatal(code as c_int);
    }
}

never finish(i32 code) {
    raw fn(c_int) -> never fatal = probe_fatal;
    if (code > 9) {
        fire(fatal, code);
    }
    unsafe {
        c_int ignored = probe_call_fatal(fatal, code as c_int);
        ignored as void;
    }
    panic("the fatal callback returned");
}

i32 main() {
    if (checked(5) != 5) {
        return 1;
    }
    finish(checked(0));
}
