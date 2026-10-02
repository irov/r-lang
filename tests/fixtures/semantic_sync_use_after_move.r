module test.semantic.sync_use_after_move;

struct Value { i32 number; };
drop(Value* self) { self->number as void; }
i32 main() {
    std.sync::once_lock<Value> once = std.sync::once_lock::<Value>();
    Value source = {.number = 1};
    std.sync::set_result<Value> value = std.sync::set(&once, move source);
    drop value;
    return source.number;
}
