module test.codegen.async_static_conditions;
struct Child { own i32* data; };
struct Parent { Child child; };
error Failure { Parent payload; };
drop(Child* self) { *(self->data) = 9; }
drop(Parent* self) { *(self->child.data) = 8; }

@generic<T: send & unborrowed>
async T relay(T value) {
    @if (T is copy) { return value; }
    @else { return move value; }
}

@generic<E: error & send & unborrowed>
async void reject(E failure) throws E {
    @if (E is copy) { throw failure; }
    @else { throw move failure; }
}

async i32 main() {
    @if (!(core::profile is hosted-native-async)) { return inactive_profile; }
    i32 finalizers = 0;
    try {
        i32 copied = await relay(42);
        if (copied != 42) { return 1; }
        Parent source = {.child = Child {.data = new i32(7)}};
        Parent moved = await relay(move source);
        if (*(moved.child.data) != 7) { return 2; }
        drop moved;
        Failure failure = {.payload = Parent {.child = Child {.data = new i32(7)}}};
        try {
            await reject(move failure);
        } catch (Failure caught) {
            if (*(caught.payload.child.data) != 7) { return 3; }
            finalizers += 1;
        } finally { finalizers += 1; }
    } catch (std.async::start_error failure) { return 4; }
    i32 selected = finalizers == 2 ? 0 : 5;
    return selected;
}
