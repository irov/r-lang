module audit.std_c_callback_module;

@callback
@export_name("r_audit_module_increment")
@safety("AUDIT-MODULE-INCREMENT", "The runtime is initialized and the caller may enter R")
extern "C" c_int increment(c_int value) {
    return value + 1i32 as c_int;
}

@generic<T: copy>
struct Box { T value; };

@generic<T: copy>
T identity(T value) { return move value; }

raw fn(c_int) -> c_int choose() { return increment; }

c_int invoke(raw fn(c_int) -> c_int callback, c_int value) {
    unsafe {
        c_int result = callback(value);
        return result;
    }
}
