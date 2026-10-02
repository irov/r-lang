module audit.std_c_raw_fn;

@callback
@export_name("r_audit_\x69ncrement")
@safety("AUDIT-INCREMENT", "The runtime is initialized and the caller may enter R")
extern "C" c_int increment(c_int value) {
    return value + 1i32 as c_int;
}

@callback
@safety("AUDIT-DOUBLE", "The runtime is initialized and the caller may enter R")
extern "C" c_int double_value(c_int value) {
    return value * 2i32 as c_int;
}

@callback
@safety("AUDIT-CONST-PARAMETER", "The runtime is initialized and the caller may enter R")
extern "C" c_int keep_const(const c_int value) {
    return value;
}

@callback
@export_name("r_audit_rounding")
@safety("AUDIT-ROUNDING", "The runtime is initialized and the caller may enter R")
extern "C" c_double add_values(c_double left, c_double right) {
    return left + right;
}

@callback
@export_name("r_audit_invoke")
@safety("AUDIT-INVOKE", "The callback has the exact C prototype and may be called once; the runtime is initialized")
extern "C" c_double invoke_external(raw fn(c_double, c_double) -> c_double callback) {
    unsafe {
        c_double result = callback(1.0 as c_double, 0.0 as c_double);
        return result + 1.1102230246251565e-16 as c_double;
    }
}

@callback
@export_name("r_audit_select")
@safety("AUDIT-SELECT", "The runtime is initialized and the caller may enter R")
extern "C" raw fn(c_int) -> c_int select_callback(c_bool doubled) {
    if (doubled == 1i32 as c_bool) { return double_value; }
    return increment;
}

struct Callbacks {
    raw fn(c_int) -> c_int first;
    raw fn?(c_int) -> c_int second;
};

raw fn(c_int) -> c_int identity(raw fn(c_int) -> c_int callback) {
    return callback;
}

raw fn(c_int) -> c_int choose(bool doubled) {
    if (doubled == true) { return double_value; }
    return increment;
}

c_int invoke(raw fn(c_int) -> c_int callback, c_int value) {
    unsafe {
        c_int result = callback(value);
        return result;
    }
}

i32 main() {
    raw fn(c_int) -> c_int callback = increment;
    raw fn(c_int) -> c_int copied = identity(callback);
    Callbacks callbacks = Callbacks { .first = copied, .second = null };
    unsafe {
        if (callbacks.second != null) { return 1; }
        if (callbacks.first(20i32 as c_int) != 21i32 as c_int) { return 2; }
        callbacks.second = double_value;
        if (callbacks.second == null) { return 3; }
        if (callbacks.second(21i32 as c_int) != 42i32 as c_int) { return 4; }
        if (copied(1i32 as c_int) != 2i32 as c_int) { return 5; }
    }
    raw fn(c_int) -> c_int selected = choose(true);
    if (invoke(selected, 21i32 as c_int) != 42i32 as c_int) { return 6; }
    raw fn(c_int) -> c_int other = choose(false);
    if (invoke(other, 41i32 as c_int) != 42i32 as c_int) { return 7; }
    raw fn(c_int) -> c_int canonical_const = keep_const;
    raw fn(const c_int) -> c_int written_const = keep_const;
    raw fn?(c_int) -> c_int absent = null;
    bool select_present = true;
    raw fn?(c_int) -> c_int present = select_present == true ? callback : absent;
    raw fn?(c_int) -> c_int missing = select_present == true ? absent : callback;
    unsafe {
        if (canonical_const(42i32 as c_int) != 42i32 as c_int ||
            written_const(42i32 as c_int) != 42i32 as c_int) { return 8; }
        if (present == null || missing != null) { return 9; }
        if (present(41i32 as c_int) != 42i32 as c_int) { return 10; }
    }
    return 0;
}
