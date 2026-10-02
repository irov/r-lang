module generic_drop;
@generic<T> struct Box { T value; own i32* marker; };
@generic<T> drop(Box<T>* self) { *(self->marker) = 9; }
i32 main() {
    Box<i32> box = Box<i32> { .value = 7, .marker = new i32(1) };
    drop box;
    Box<own i32*> other = Box<own i32*> { .value = new i32(17), .marker = new i32(1) };
    drop other;
    return 0;
}
