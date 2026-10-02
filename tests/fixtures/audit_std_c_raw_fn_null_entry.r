module audit.std_c_raw_fn_null_entry;

@callback
@export_name("r_audit_nonnull_callback")
@safety("AUDIT-NONNULL-CALLBACK", "The runtime is initialized and callback is non-null")
extern "C" void accept_callback(raw fn() -> void callback) {
    callback as void;
}

i32 main() { return 0; }
