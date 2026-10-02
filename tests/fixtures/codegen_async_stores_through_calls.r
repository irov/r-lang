module test.codegen.async_stores_through_calls;

/* R-BORROW-0018, R-BORROW-0024: an async frame stores borrows through borrows that calls return
   from its own containers of exclusive borrows, and ends every such borrow before an await. */

(str)* first_slot(array<(str)*>* slots) {
    return (*slots)[0];
}

array<str>* first_list(array<array<str>*>* lists) {
    return (*lists)[0];
}

str* pick(array<str>* words) {
    return &(*words)[0];
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async usize work() throws std.async::start_error {
    i32 step = await tick(1);
    str a = "a";
    str text = "hello";
    array<str> words = std.array::create::<str>();
    array<(str)*> slots = std.array::create::<(str)*>();
    array<array<str>*> lists = std.array::create::<array<str>*>();
    bool failed = false;
    try {
        std.array::push(&slots, &a);
        *first_slot(&slots) = text;
        std.array::push(&lists, &words);
        std.array::push(first_list(&lists), text);
    } catch (std.array::push_error<(str)*> e) {
        move e as void;
        failed = true;
    } catch (std.array::push_error<array<str>*> e) {
        move e as void;
        failed = true;
    } catch (std.array::push_error<str> e) {
        move e as void;
        failed = true;
    }
    drop slots;
    drop lists;
    str first = *pick(&words);
    usize total = len(a) + len(words) + len(first);
    if ((step != 2) || (failed == true)) {
        return 100usize;
    }
    return total;
}

async i32 main() {
    try {
        return await work() as i32 - 11;
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
