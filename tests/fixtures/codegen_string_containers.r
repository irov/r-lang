module test.codegen.string_containers;

/* R-BORROW-0018, R-LIB-0019..R-LIB-0022, R-EXPR-0030: `array<str>`, `list<str>` and dictionaries
   with string keys and values. A stored string brings its origins into the container, a string
   taken out carries them, and a borrow of the container designates its storage. */

usize total(const array<str>* names) {
    const str[] view = std.array::as_slice(names);
    usize sum = 0usize;
    for (usize index = 0usize; index < len(view); index += 1usize) { sum += len(view[index]); }
    return sum;
}
void add_fixed(array<str>* names) throws std.array::push_error<str> {
    std.array::push(names, "fixed");
}
array<str> build(str first, str second) throws std.array::push_error<str> {
    array<str> names = std.array::create::<str>();
    std.array::push(&names, first);
    std.array::push(&names, second);
    return move names;
}
usize arrays(str text) throws std.array::push_error<str> {
    array<str> names = build(text, "beta");
    const str[] view = std.array::as_slice(&names);
    str copy = view[0];
    std.array::push(&names, copy);
    add_fixed(&names);
    o<str> last = std.array::pop(&names);
    usize last_len = 0usize;
    switch (last) {
        case variant o::some(value): last_len = len(*value); break;
        case variant o::none: break;
    }
    str[] editable = std.array::as_slice_mut(&names);
    editable[1] = "gamma";
    return total(&names) + last_len;
}

usize list_total(const list<str>* names) {
    usize sum = 0usize;
    for (const str* name in names) { sum += len(*name); }
    return sum;
}
usize lists(str text) throws std.list::push_error<str> {
    list<str> names = std.list::create::<str>();
    std.list::push_back(&names, text) as void;
    std.list::push_front(&names, "zero") as void;
    o<str> popped = std.list::pop_front(&names);
    usize popped_len = 0usize;
    switch (popped) {
        case variant o::some(value): popped_len = len(*value); break;
        case variant o::none: break;
    }
    return list_total(&names) + popped_len;
}

usize dicts(str key, str value) throws std.dict::insert_error<str, str> {
    dict<str, str> table = std.dict::create::<str, str>();
    o<str> old = std.dict::insert(&table, key, value);
    old as void;
    usize found = 0usize;
    switch (std.dict::get(&table, &key)) {
        case variant o::some(item): found = len(**item); break;
        case variant o::none: break;
    }
    return found + len(table);
}

usize literals(str text) throws std.array::push_error<str> {
    array<str> names = [text, "beta"];
    usize sum = 0usize;
    for (const str* name in &names) { sum += len(*name); }
    array<str> copies = [*name for (const str* name in &names) if (len(*name) > 1usize)];
    return sum + len(copies);
}

i32 main() {
    try {
        if (arrays("alpha") != 20usize) { return 1; }
        if (lists("abc") != 7usize) { return 2; }
        if (dicts("k", "vvv") != 4usize) { return 3; }
        if (literals("al") != 8usize) { return 4; }
    } catch (std.array::push_error<str> failure) { failure as void; return 5; }
    catch (std.list::push_error<str> failure) { failure as void; return 6; }
    catch (std.dict::insert_error<str, str> failure) { failure as void; return 7; }
    return 0;
}
