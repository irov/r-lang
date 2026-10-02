module audit.std_c_raw_fn_null;

i32 main() {
    raw fn?() -> void callback = null;
    unsafe { callback(); }
    return 1;
}
