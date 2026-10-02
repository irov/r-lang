module fixture.cmp_impls;

import std.cmp;
import std.string;

/* R-SLIB-CMP-0001 (L27): implementations over standard types, dispatched through a generic
   constraint; o<T>, T[N], slices and array<T> are found by the shape of the instance (L27-2). */
@generic<T: std.cmp::Ordered>
bool before(const T* left, const T* right) {
    std.cmp::ordering order = left->cmp(right);
    return order == std.cmp::ordering::less;
}

@generic<T: std.cmp::Equal>
bool same(const T* left, const T* right) {
    return left->eq(right);
}

/* R-FUNC-0009 (L27): hooks that add `key` to the parameter; Box<Plain> has no key hooks. */
@generic<T>
struct Box { T first; T second; };

@generic<T: key>
u64 Box<T>::hash(const Box<T>* value) {
    u64 first = (14695981039346656037u64 ^ core::hash(&value->first)) * 1099511628211u64;
    u64 second = (first ^ core::hash(&value->second)) * 1099511628211u64;
    return second;
}

@generic<T: key>
bool Box<T>::equal(const Box<T>* left, const Box<T>* right) {
    if (core::key_equal(&left->first, &right->first) == false) { return false; }
    return core::key_equal(&left->second, &right->second);
}

struct Plain { i32 v; };

enum Weekday { monday = 1, tuesday = 2, sunday = 7 };

/* L27-3: a match over an enum without payloads compares enum constants. */
u32 rank(Weekday day) {
    return match (day) {
    case variant Weekday::sunday: 0u32;
    case variant Weekday::monday: 1u32;
    case variant Weekday::tuesday: 2u32;
    };
}

/* R-STMT-0010 (L27-1): a switch over a Move o place without outer move borrows it. */
usize text_length(const (o<std.string::string>)* value) {
    switch (*value) {
    case variant o::some(text): return text->len();
    case variant o::none: return 0usize;
    }
    return 0usize;
}

i32 check_text() {
    str a = "apple";
    str b = "apricot";
    if (before(&a, &b) == false || same(&a, &a) == false) { return 1; }
    str empty = "";
    if (before(&empty, &a) == false) { return 2; }
    return 0;
}

i32 check_options() {
    o<i32> none = o::none;
    o<i32> some = o::some(1);
    if (before(&none, &some) == false || same(&some, &some) == false) { return 10; }
    o<o<i32>> nested = o::some(o::some(4));
    o<o<i32>> hollow = o::some(o::none);
    if (same(&nested, &hollow) == true || before(&hollow, &nested) == false) { return 11; }
    return 0;
}

i32 check_sequences() {
    (i32, str) t1 = (1, "b");
    (i32, str) t2 = (1, "c");
    if (before(&t1, &t2) == false || same(&t1, &t1) == false) { return 20; }
    (i32, i32, i32, i32, i32, i32, i32, i32) wide1 = (1, 2, 3, 4, 5, 6, 7, 8);
    (i32, i32, i32, i32, i32, i32, i32, i32) wide2 = (1, 2, 3, 4, 5, 6, 7, 9);
    if (before(&wide1, &wide2) == false) { return 21; }
    i32[3] f1 = {1, 2, 3};
    i32[3] f2 = {1, 2, 4};
    if (before(&f1, &f2) == false || same(&f1, &f1) == false) { return 22; }
    const i32[] s1 = f1[0usize..2usize];
    const i32[] s2 = f1[0usize..3usize];
    if (before(&s1, &s2) == false || same(&s1, &s2) == true) { return 23; }
    return 0;
}

i32 check_owned() throws std.alloc::alloc_error, std.array::push_error<std.string::string> {
    std.string::string x = std.string::from_str("ab");
    std.string::string y = std.string::from_str("abc");
    if (before(&x, &y) == false || same(&x, &x) == false) { return 30; }
    array<std.string::string> p = std.array::create();
    p.push(std.string::from_str("a"));
    array<std.string::string> q = std.array::create();
    q.push(std.string::from_str("b"));
    if (before(&p, &q) == false || p.eq(&q) == true) { return 31; }
    o<std.string::string> maybe = o::some(std.string::from_str("hey"));
    if (text_length(&maybe) != 3usize) { return 32; }
    switch (maybe) {
    case variant o::some(kept): if (kept->len() != 3usize) { return 33; }
    case variant o::none: return 34;
    }
    switch (move maybe) {
    case variant o::some(move owned): if (owned.len() != 3usize) { return 35; }
    case variant o::none: return 36;
    }
    return 0;
}

i32 check_hooks() {
    Box<i32> first = {.first = 1, .second = 2};
    Box<i32> second = {.first = 1, .second = 2};
    if (core::key_equal(&first, &second) == false) { return 40; }
    Box<Plain> plain = {.first = Plain {.v = 1}, .second = Plain {.v = 2}};
    if (plain.first.v != 1) { return 41; }
    if (rank(Weekday::sunday) != 0u32 || rank(Weekday::tuesday) != 2u32) { return 42; }
    return 0;
}

i32 main() {
    i32 text = check_text();
    if (text != 0) { return text; }
    i32 options = check_options();
    if (options != 0) { return options; }
    i32 sequences = check_sequences();
    if (sequences != 0) { return sequences; }
    try {
        i32 owned = check_owned();
        if (owned != 0) { return owned; }
    } catch (std.alloc::alloc_error failure) {
        return 50;
    } catch (std.array::push_error<std.string::string> failure) {
        return 51;
    }
    return check_hooks();
}
