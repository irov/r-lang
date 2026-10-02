module test.codegen.owner_element_containers;

/* R-LIB-0019..R-LIB-0022, R-OBJ-0006: containers of owners of distinct payload types in one
   program each get their own element operations, and a catch clause for an insertion error
   that no call in its try block can raise is valid. */

struct Left {
    i32 value;
};

struct Right {
    i32 value;
};

i32 shared_lists() throws std.list::push_error<arc Left>, std.list::push_error<arc Right>,
    std.list::push_error<rc Left> {
    list<arc Left> lefts = std.list::create::<arc Left>();
    list<arc Right> rights = std.list::create::<arc Right>();
    list<rc Left> counted = std.list::create::<rc Left>();
    arc Left left = new arc Left {.value = 1};
    std.list::push_back(&lefts, std.arc::clone(&left)) as void;
    std.list::push_front(&lefts, move left) as void;
    std.list::push_back(&rights, new arc Right {.value = 10}) as void;
    std.list::push_back(&counted, new rc Left {.value = 100}) as void;
    i32 sum = 0;
    for (const (arc Left)* item in &lefts) { sum += (*item)->value; }
    for (const (arc Right)* item in &rights) { sum += (*item)->value; }
    for (const (rc Left)* item in &counted) { sum += (*item)->value; }
    return sum;
}

i32 owned_arrays() throws std.array::push_error<own Left*>, std.array::push_error<own Right*>,
    std.dict::insert_error<i32, arc Right> {
    array<own Left*> lefts = std.array::create::<own Left*>();
    array<own Right*> rights = std.array::create::<own Right*>();
    std.array::push(&lefts, new Left {.value = 2});
    std.array::push(&rights, new Right {.value = 20});
    std.array::push(&rights, new Right {.value = 30});
    dict<i32, arc Right> table = std.dict::create::<i32, arc Right>();
    std.dict::insert(&table, 7, new arc Right {.value = 200}) as void;
    i32 sum = 0;
    for (const (own Left*)* item in &lefts) { sum += (*item)->value; }
    for (const (own Right*)* item in &rights) { sum += (*item)->value; }
    i32 key = 7;
    switch (std.dict::get(&table, &key)) {
    case variant o::some(found):
        sum += (**found)->value;
        break;
    case variant o::none:
        return -1;
    }
    return sum;
}

i32 unreachable_catch() {
    i32 sum = 0;
    try {
        array<own Left*> empty = std.array::create::<own Left*>();
        for (const (own Left*)* item in &empty) { sum += (*item)->value; }
    } catch (std.array::push_error<own Left*> failure) {
        move failure as void;
        return -1;
    }
    return sum;
}

i32 main() {
    try {
        if (shared_lists() != 112) {
            return 1;
        }
    } catch (std.list::push_error<arc Left> failure) {
        move failure as void;
        return 90;
    } catch (std.list::push_error<arc Right> failure) {
        move failure as void;
        return 91;
    } catch (std.list::push_error<rc Left> failure) {
        move failure as void;
        return 92;
    }
    try {
        if (owned_arrays() != 252) {
            return 2;
        }
    } catch (std.array::push_error<own Left*> failure) {
        move failure as void;
        return 93;
    } catch (std.array::push_error<own Right*> failure) {
        move failure as void;
        return 94;
    } catch (std.dict::insert_error<i32, arc Right> failure) {
        move failure as void;
        return 95;
    }
    if (unreachable_catch() != 0) {
        return 3;
    }
    return 0;
}
