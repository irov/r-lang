module test.regression.list_lookup_live_alias;

i32 main() {
    try {
        list<i32> values = std.list::create::<i32>();
        i32* inserted = std.list::push_back(&values, 7);
        o<i32*> found = std.list::get_mut(&values, 0usize);
        switch (move found) {
        case variant o::some(pointer):
            i32* node = *pointer;
            const i32* alias = node;
            i32 removed = std.list::remove(&values, move node);
            return removed + *alias;
        case variant o::none: return 1;
        }
    } catch (std.list::push_error<i32> failure) { return 2; }
}
