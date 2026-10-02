module test.semantic.sync_missing_move;

struct Value { i32 number; };
drop(Value* self) { self->number as void; }
i32 main() {
    std.sync::once_lock<Value> once = std.sync::once_lock::<Value>();
    Value source = {.number = 1};
    std.sync::set_result<Value> value = std.sync::set(&once, source);
    drop value;
    return 0;
}
