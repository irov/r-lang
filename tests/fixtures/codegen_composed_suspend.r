module test.codegen.composed_suspend;

struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
Owner create(i32 value) { return Owner {.value = new i32(value)}; }
i32 consume(Owner value, i32 addition) { return *value.value + addition; }

async i32 evaluate(task<i32> pending) {
    return consume(create(22), await move pending);
}

async i32 child() { return 20; }

async i32 main() {
    try {
        i32 result = await evaluate(child());
        i32 selected = result == 42 ? 0 : 1;
        return selected;
    } catch (std.async::start_error failure) { return 2; }
}
