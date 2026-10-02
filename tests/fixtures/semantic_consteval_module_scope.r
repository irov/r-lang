module test.semantic.consteval_module_scope;

/* R-EXPR-0032: a module-scope type bound computed by a call that panics. */
usize frame_size() {
    panic("no frame size");
}

struct Frame {
    u8[frame_size()] bytes;
};

i32 main() {
    return 0;
}
