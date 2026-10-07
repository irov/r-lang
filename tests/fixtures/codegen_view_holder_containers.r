module test.codegen.view_holder_containers;

/* R-BORROW-0002, R-BORROW-0018, R-LIB-0019..R-LIB-0022: containers whose elements hold views
   in any form. Element access reaches the container storage; what an element holds stays a
   shared loan of its own source; exclusive elements are written through exclusive access and
   attenuated through shared access. */

struct Named { str name; i32 id; };

usize total_names(const list<Named>* items) {
    usize total = 0usize;
    for (const Named* item in items) { total += len(item->name); }
    return total;
}

str first_word(const str[] words) {
    for (str word in &words) { return word; }
    return "";
}

i32 sum_slots(list<i32*> slots) {
    i32 total = 0;
    for (const (i32*)* slot in &slots) { total += **slot; }
    return total;
}

void bump_front(list<i32*>* slots) {
    switch (std.list::front_mut(slots)) {
    case variant o::some(slot): ***slot += 100; break;
    case variant o::none: break;
    }
}

/* Structs, options and containers as elements. */
usize view_holders(str short, str long)
    throws std.list::push_error<Named>, std.array::push_error<Named>,
    std.dict::insert_error<i32, Named>, std.array::push_error<o<str>>,
    std.array::push_error<str>, std.array::push_error<array<str>> {
    list<Named> items = std.list::create::<Named>();
    std.list::push_back(&items, Named {.name = short, .id = 1}) as void;
    std.list::push_back(&items, Named {.name = short, .id = 2}) as void;
    switch (std.list::front_mut(&items)) {
    case variant o::some(front): (*front)->name = long; break;
    case variant o::none: break;
    }
    switch (std.list::back_mut(&items)) {
    case variant o::some(back): (*back)->id = 20; break;
    case variant o::none: break;
    }
    usize listed = total_names(&items);
    o<Named> popped = std.list::pop_front(&items);
    switch (popped) {
    case variant o::some(value): listed += len(value->name); break;
    case variant o::none: break;
    }

    array<Named> named = [Named {.name = short, .id = 3}, Named {.name = long, .id = 4}];
    str second = "";
    switch (std.array::get(&named, 1usize)) {
    case variant o::some(entry): second = (*entry)->name; break;
    case variant o::none: break;
    }
    switch (std.array::get_mut(&named, 0usize)) {
    case variant o::some(entry): (*entry)->name = second; break;
    case variant o::none: break;
    }
    for (Named* item in &named) { item->id += 10; }
    usize arrayed = 0usize;
    for (const Named* item in &named) { arrayed += len(item->name) + (item->id as usize); }

    dict<i32, Named> index = std.dict::create::<i32, Named>();
    std.dict::insert(&index, 7, Named {.name = short, .id = 7}) as void;
    i32 key = 7;
    switch (std.dict::get_mut(&index, &key)) {
    case variant o::some(found): (*found)->name = long; break;
    case variant o::none: break;
    }
    usize looked = 0usize;
    switch (std.dict::get(&index, &key)) {
    case variant o::some(found): looked = len((*found)->name); break;
    case variant o::none: break;
    }

    array<o<str>> maybe = std.array::create::<o<str>>();
    std.array::push(&maybe, o::some(long));
    std.array::push(&maybe, o::none);
    usize present = 0usize;
    for (const (o<str>)* entry in &maybe) {
        switch (*entry) {
        case variant o::some(word): present += len(*word); break;
        case variant o::none: break;
        }
    }

    array<array<str>> groups = std.array::create::<array<str>>();
    array<str> inner = std.array::create::<str>();
    std.array::push(&inner, short);
    std.array::push(&groups, move inner);
    switch (std.array::get_mut(&groups, 0usize)) {
    case variant o::some(group): std.array::push(*group, long); break;
    case variant o::none: break;
    }
    usize nested = 0usize;
    for (const array<str>* group in &groups) {
        for (const str* word in group) { nested += len(*word); }
    }
    /* short = "ab", long = "abcdef": listed 6+2 then 6 popped; arrayed (6+13)+(6+14);
       looked 6; present 6; nested 2+6 */
    return listed + arrayed + looked + present + nested;
}

/* Shared borrows and slices of view holders as elements. */
usize shared_views(str text) {
    str alias = text;
    Named holder = {.name = text, .id = 1};
    str[2] words = {text, "xyz"};
    array<const str*> views = std.array::create::<const str*>();
    list<const Named*> refs = std.list::create::<const Named*>();
    array<const str[]> spans = std.array::create::<const str[]>();
    try {
        std.array::push(&views, &alias);
        std.list::push_back(&refs, &holder) as void;
        std.array::push(&spans, words[0usize..2usize]);
    } catch (std.array::push_error<const str*> e) {
        move e as void;
    } catch (std.list::push_error<const Named*> e) {
        move e as void;
    } catch (std.array::push_error<const str[]> e) {
        move e as void;
    }
    usize total = 0usize;
    for (const (const str*)* view in &views) { total += len(**view); }
    for (const (const Named*)* item in &refs) { total += len((*item)->name); }
    for (const (const str[])* span in &spans) {
        for (const str* word in span) { total += len(*word); }
    }
    /* text = "ab": 2 + 2 + (2 + 3) */
    return total;
}

/* Exclusive borrows and mutable slices as elements. */
i32 exclusive_views() {
    i32 a = 1;
    i32 b = 2;
    i32 c = 3;
    i32[2] x = {10, 20};
    i32[2] y = {30, 40};
    array<i32*> slots = std.array::create::<i32*>();
    list<i32*> queue = std.list::create::<i32*>();
    array<i32[]> parts = std.array::create::<i32[]>();
    try {
        std.array::push(&slots, &a);
        std.list::push_back(&queue, &b) as void;
        std.list::push_back(&queue, &c) as void;
        std.array::push(&parts, x[0usize..2usize]);
        std.array::push(&parts, y[0usize..2usize]);
    } catch (std.array::push_error<i32*> e) {
        move e as void;
    } catch (std.list::push_error<i32*> e) {
        move e as void;
    } catch (std.array::push_error<i32[]> e) {
        move e as void;
    }
    for ((i32*)* slot in &slots) { **slot += 5; }
    bump_front(&queue);
    for ((i32[])* part in &parts) { (*part)[1] += 1; }
    o<i32*> last = std.array::pop(&slots);
    switch (move last) {
    case variant o::some(slot): **slot *= 2; break;
    case variant o::none: break;
    }
    i32 queued = sum_slots(move queue);
    std.array::clear(&parts);
    /* a = (1 + 5) * 2 = 12; b = 102, c = 3, queued 105; x[1] = 21, y[1] = 41 */
    return a + b + c + queued + x[1] + y[1];
}

usize staged_literals() throws std.alloc::alloc_error {
    std.string::string owner = std.string::from_str("owned");
    Named named = {.name = owner, .id = 1};
    str[2] pair = {owner, "b"};
    return len(named.name) + len(pair[0]) + len(pair[1]);
}

i32 main() {
    const str[3] words = {"first", "second", "third"};
    str first = first_word(words[0usize..3usize]);
    if (len(first) != 5usize) { return 1; }
    try {
        usize holders = view_holders("ab", "abcdef");
        if (holders != 14usize + 39usize + 6usize + 6usize + 8usize) { return 2; }
    } catch (std.list::push_error<Named> e) {
        move e as void;
        return 3;
    } catch (std.array::push_error<Named> e) {
        move e as void;
        return 3;
    } catch (std.dict::insert_error<i32, Named> e) {
        move e as void;
        return 3;
    } catch (std.array::push_error<o<str>> e) {
        move e as void;
        return 3;
    } catch (std.array::push_error<str> e) {
        move e as void;
        return 3;
    } catch (std.array::push_error<array<str>> e) {
        move e as void;
        return 3;
    }
    if (shared_views("ab") != 9usize) { return 4; }
    if (exclusive_views() != 12 + 102 + 3 + 105 + 21 + 41) { return 5; }
    try {
        if (staged_literals() != 11usize) { return 6; }
    } catch (std.alloc::alloc_error e) {
        move e as void;
        return 7;
    }
    return 0;
}
