module async_generic_drop;
@generic<T> struct Box { T value; own i32* marker; };
@generic<T> drop(Box<T>* self) { *(self->marker) = 9; }
async void checkpoint() { return; }
async i32 main() {
    try {
        Box<i32> box = Box<i32> { .value = 7, .marker = new i32(1) };
        task<void> first = checkpoint();
        await move first;
        drop box;
        Box<own i32*> other = Box<own i32*> { .value = new i32(17), .marker = new i32(1) };
        task<void> second = checkpoint();
        await move second;
        drop other;
    } catch (std.async::start_error error) { return 1; }
    return 0;
}
