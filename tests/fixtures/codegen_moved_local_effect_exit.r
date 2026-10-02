module test.codegen.moved_local_effect_exit;

/*
 * A Move local that is consumed by a later call is still dropped by the effect-exit
 * cleanups of the checked calls that precede the move. The emitter must declare the drop
 * glue for such a local even though no ordinary scope-end drop remains.
 */
protected i32 checks() {
    try {
        bytes staged = {};
        std.bytes::append_u8(&staged, 0x41);
        std.bytes::append_u8(&staged, 0x42);
        std.string::from_bytes_result converted = std.string::from_bytes(move staged);
        drop converted;
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 9;
    }
}

i32 main() {
    i32 outcome = checks();
    return outcome;
}
