module test.codegen.caught_borrow_errors;

/* R-ERR-0002, R-BORROW-0018: a caught checked error that holds a container of exclusive borrows
   is rethrown by `throw move e` and `throw;` or destroyed by `drop e`; the borrows it holds keep
   the origins of the thrown arguments. */

error Bad {
    array<i32*> slots;
};

void fail(i32* slot) throws Bad, std.array::push_error<i32*> {
    array<i32*> slots = std.array::create::<i32*>();
    std.array::push(&slots, slot);
    throw Bad {.slots = move slots};
}

void rethrow_named(i32* slot) throws Bad, std.array::push_error<i32*> {
    try {
        fail(slot);
    } catch (Bad e) {
        throw move e;
    }
}

void rethrow_bare(i32* slot) throws Bad, std.array::push_error<i32*> {
    try {
        fail(slot);
    } catch (Bad e) {
        throw;
    }
}

void dropped(i32* slot) throws std.array::push_error<i32*> {
    try {
        fail(slot);
    } catch (Bad e) {
        drop e;
    }
}

i32 main() {
    i32 a = 1;
    try {
        dropped(&a);
        rethrow_bare(&a);
        return 1;
    } catch (Bad e) {
        if (len(e.slots) != 1usize) { return 2; }
        *e.slots[0] += 4;
    } catch (std.array::push_error<i32*> e) {
        move e as void;
        return 3;
    }
    try {
        rethrow_named(&a);
        return 4;
    } catch (Bad e) {
        *e.slots[0] += 5;
    } catch (std.array::push_error<i32*> e) {
        move e as void;
        return 5;
    }
    return a - 10;
}
