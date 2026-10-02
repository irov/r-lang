module test.codegen.dict_custom_keys;

struct Key { i32 value; };
u64 Key::hash(const Key* value) { return (value->value % 10) as u64; }
bool Key::equal(const Key* left, const Key* right) {
    return left->value % 10 == right->value % 10;
}
@generic<T: key & copy> struct Box { T value; };
@generic<T: key & copy> u64 Box<T>::hash(const Box<T>* value) {
    u64 result = core::hash(&value->value); return result;
}
@generic<T: key & copy> bool Box<T>::equal(const Box<T>* left, const Box<T>* right) {
    bool result = core::key_equal(&left->value, &right->value); return result;
}

i32 synchronous() throws std.alloc::alloc_error, std.dict::insert_error<Key, i32> {
    dict<Key, i32> cache = std.dict::create::<Key, i32>();
    Key first = Key { .value = 11 };
    Key alias = Key { .value = 21 };
    o<i32> previous = std.dict::insert(&cache, first, 7);
    previous as void;
    o<i32> replaced = std.dict::insert(&cache, alias, 9);
    switch (replaced) {
    case variant o::some(value): if (*value != 7) { return 1; } break;
    case variant o::none: return 2;
    }
    if (len(cache) != 1usize) { return 3; }
    o<i32> removed = std.dict::remove(&cache, &first);
    switch (removed) {
    case variant o::some(value): if (*value != 9) { return 4; } break;
    case variant o::none: return 5;
    }
    return 0;
}
async i32 asynchronous() throws std.alloc::alloc_error, std.dict::insert_error<Box<Key>, i32> {
    dict<Box<Key>, i32> cache = std.dict::create::<Box<Key>, i32>();
    Box<Key> first = Box<Key> { .value = Key { .value = 12 } };
    Box<Key> alias = Box<Key> { .value = Key { .value = 32 } };
    Box<Key> absent = Box<Key> { .value = Key { .value = 33 } };
    o<i32> previous = std.dict::insert(&cache, first, 17);
    previous as void;
    bool present = std.dict::contains(&cache, &alias);
    bool missing = std.dict::contains(&cache, &absent);
    missing as void;
    if (present != true || missing != false) { return 6; }
    o<const i32*> found = std.dict::get(&cache, &alias);
    switch (found) {
    case variant o::some(value): if (**value != 17) { return 7; } break;
    case variant o::none: return 8;
    }
    return 0;
}
async i32 main() {
    try {
        try {
            i32 first = synchronous();
            i32 second = await asynchronous();
            return first + second;
        } catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 10}; }
        catch (std.dict::insert_error<Key, i32> failure) { throw TestAssertionFailed {.code = 11}; }
        catch (std.dict::insert_error<Box<Key>, i32> failure) { throw TestAssertionFailed {.code = 12}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 13}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
