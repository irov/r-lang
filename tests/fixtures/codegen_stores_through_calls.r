module test.codegen.stores_through_calls;

/* R-BORROW-0018, R-BORROW-0019: a borrow stored through a borrow a call returns reaches the
   storage that the argument's exclusive borrows designate: `*first_slot(&slots) = text` writes the
   str that the element of `array<(str)*>` borrows, and `std.array::push(first_list(&lists), text)`
   grows the array that the element of `array<array<str>*>` borrows. Through a parameter the stored
   borrow derives from that parameter. A value read through a borrow a call returns carries what
   the designated storage holds. */

(str)* first_slot(array<(str)*>* slots) {
    return (*slots)[0];
}

(str)* last_slot(array<(str)*>* slots) {
    return (*slots)[len(*slots) - 1usize];
}

array<str>* first_list(array<array<str>*>* lists) {
    return (*lists)[0];
}

str* pick(array<str>* words) {
    return &(*words)[0];
}

usize count(array<str>* words) {
    return len(*words);
}

/* The stored view comes from the same parameter. */
void rotate(array<(str)*>* slots) {
    str last = *last_slot(slots);
    *first_slot(slots) = last;
}

usize run(str text) {
    str a = "a";
    str b = "b";
    array<str> words = std.array::create::<str>();
    array<(str)*> slots = std.array::create::<(str)*>();
    array<array<str>*> lists = std.array::create::<array<str>*>();
    bool failed = false;
    try {
        std.array::push(&slots, &a);
        std.array::push(&slots, &b);
        *first_slot(&slots) = text;
        std.array::push(&lists, &words);
        std.array::push(first_list(&lists), text);
        std.array::push(first_list(&lists), "tail");
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
    rotate(&slots);
    drop slots;
    drop lists;
    /* Reading through `pick` copies the view out; the array stays usable. */
    str first = *pick(&words);
    usize total = len(a) + len(b) + count(&words) + len(first);
    if (failed == true) {
        return 100usize;
    }
    return total;
}

i32 main() {
    /* a = "hello", b = "b" rotated into a, words = {"hello", "tail"}: 1 + 1 + 2 + 5. */
    return run("hello") as i32 - 9;
}
