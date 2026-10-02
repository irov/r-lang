module test.regression.dict_mutation_borrow_rule;

i32 main() {
    try {
        dict<i32, i32> values = std.dict::with_capacity::<i32, i32>(1);
        o<i32> first = std.dict::insert(&values, 1, 7);
        first as void;
        dict<i32, i32>* target = &values;
        const dict<i32, i32>* source = target;
        i32 key = 1;
        o<const i32*> found = std.dict::get(source, &key);
        switch (found) {
            case variant o::some(value):
                o<i32> second = std.dict::insert(target, 2, 9);
                second as void;
                return **value;
            case variant o::none:
                return 2;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
        return 4;
    }
}
