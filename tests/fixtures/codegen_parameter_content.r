module test.codegen.parameter_content;

/* R-BORROW-0018, R-BORROW-0019: what a parameter holds or designates has a region of its own.
   A view read from it stays usable while the storage the parameter designates is changed
   through that parameter; stored values still derive from the parameter. */

struct Named { str name; i32 id; };
struct Pair { str left; str right; };

usize rename_first(array<Named>* items) {
    str second = "";
    {
        const array<Named>* shared = items;
        switch (std.array::get(shared, 1usize)) {
        case variant o::some(entry): second = (*entry)->name; break;
        case variant o::none: break;
        }
    }
    switch (std.array::get_mut(items, 0usize)) {
    case variant o::some(entry): (*entry)->name = second; break;
    case variant o::none: break;
    }
    return len(second);
}

usize swap(Pair* pair) {
    str left = pair->left;
    pair->left = pair->right;
    pair->right = left;
    return len(left);
}

usize fill(array<str>* names) {
    str first = "";
    {
        const array<str>* shared = names;
        switch (std.array::get(shared, 0usize)) {
        case variant o::some(name): first = **name; break;
        case variant o::none: break;
        }
    }
    for (str* name in names) { *name = first; }
    return len(first);
}

usize reset(array<Named>* items) throws std.array::push_error<Named> {
    str first = "";
    {
        const array<Named>* shared = items;
        switch (std.array::get(shared, 0usize)) {
        case variant o::some(entry): first = (*entry)->name; break;
        case variant o::none: break;
        }
    }
    std.array::clear(items);
    std.array::push(items, Named {.name = first, .id = 9});
    return len(first);
}

usize rotate(str[] words) {
    str first = words[0];
    words[0] = words[1];
    words[1] = words[2];
    words[2] = first;
    return len(first);
}

usize rename(Named named) {
    str old = named.name;
    named.name = "renamed";
    return len(old) + len(named.name);
}

usize total(const array<Named>* items) {
    usize sum = 0usize;
    for (const Named* item in items) { sum += len(item->name); }
    return sum;
}

i32 main() {
    array<Named> items = std.array::create::<Named>();
    try {
        std.array::push(&items, Named {.name = "a", .id = 1});
        std.array::push(&items, Named {.name = "bbb", .id = 2});
        if (rename_first(&items) != 3usize) { return 1; }
        if (total(&items) != 6usize) { return 2; }
        if (reset(&items) != 3usize) { return 3; }
        if ((len(items) != 1usize) || (total(&items) != 3usize)) { return 4; }
    } catch (std.array::push_error<Named> failure) {
        move failure as void;
        return 5;
    }
    Pair pair = {.left = "l", .right = "right"};
    if ((swap(&pair) != 1usize) || (len(pair.left) != 5usize) || (len(pair.right) != 1usize)) {
        return 6;
    }
    array<str> names = std.array::create::<str>();
    try {
        std.array::push(&names, "xy");
        std.array::push(&names, "z");
        std.array::push(&names, "w");
        if ((fill(&names) != 2usize) || (len(names) != 3usize)) { return 7; }
        for (const str* name in &names) {
            if (len(*name) != 2usize) { return 8; }
        }
    } catch (std.array::push_error<str> failure) {
        move failure as void;
        return 9;
    }
    str[3] words = {"one", "three", "five"};
    if (rotate(words[0usize..3usize]) != 3usize) { return 10; }
    if ((len(words[0]) != 5usize) || (len(words[1]) != 4usize) || (len(words[2]) != 3usize)) {
        return 11;
    }
    Named named = {.name = "abc", .id = 3};
    if (rename(named) != 10usize) { return 12; }
    return 0;
}
