module test.regression.list_lookup_wrong_origin;

i32 main() {
    try {
        list<i32> first = std.list::create::<i32>();
        list<i32> second = std.list::create::<i32>();
        i32* inserted = std.list::push_back(&first, 7);
        o<const i32*> found = std.list::get(&first, 0usize);
        switch (found) {
        case variant o::some(pointer):
            const i32* position = *pointer;
            i32* misplaced = std.list::insert_before(&second, position, 9);
            return *misplaced;
        case variant o::none: return 1;
        }
    } catch (std.list::push_error<i32> failure) { return 2; }
}
