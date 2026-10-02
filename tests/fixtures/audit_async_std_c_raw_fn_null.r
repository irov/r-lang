module audit.async_std_c_raw_fn_null;

async i32 main() {
    raw fn?() -> void callback = null;
    unsafe { callback(); }
    return 1;
}
