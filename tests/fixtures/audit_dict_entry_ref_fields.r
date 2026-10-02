module audit.dict_entry_ref_fields;

i32 main() {
    try {
        dict<i32, i32> values = std.dict::create::<i32, i32>();
        o<i32> old = std.dict::insert(&values, 1, 7);
        old as void;
        std.dict::iter<i32, i32> iterator = std.dict::iter(&values);
        o<std.dict::entry_ref<i32, i32>> next = std.dict::next(&iterator);
        switch (next) {
            case variant o::some(entry):
                return *(entry->key) + *(entry->value) - 8;
            case variant o::none:
                return 2;
        }
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
        return 3;
    }
}
