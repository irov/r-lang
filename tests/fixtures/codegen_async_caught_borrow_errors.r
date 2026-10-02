module test.codegen.async_caught_borrow_errors;

/* R-ERR-0002, R-BORROW-0024: an async frame catches a checked error that holds containers of
   borrows, rethrows it to an enclosing try of the same frame and drops it before the next
   await. */

struct Named {
    str name;
};

error Bad {
    array<i32*> slots;
    list<Named> names;
};

void fail(i32* slot, str word) throws Bad, std.array::push_error<i32*>,
    std.list::push_error<Named> {
    array<i32*> slots = std.array::create::<i32*>();
    std.array::push(&slots, slot);
    list<Named> names = std.list::create::<Named>();
    std.list::push_back(&names, Named {.name = word}) as void;
    throw Bad {.slots = move slots, .names = move names};
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error {
    i32 a = 1;
    i32 b = 2;
    str word = "w";
    try {
        try {
            fail(&a, word);
            return 1;
        } catch (Bad e) {
            throw move e;
        }
    } catch (Bad e) {
        *e.slots[0] += 4;
    } catch (std.array::push_error<i32*> e) {
        move e as void;
        return 2;
    } catch (std.list::push_error<Named> e) {
        move e as void;
        return 3;
    }
    try {
        try {
            fail(&b, word);
            return 4;
        } catch (Bad e) {
            throw;
        }
    } catch (Bad e) {
        *e.slots[0] += 5;
    } catch (std.array::push_error<i32*> e) {
        move e as void;
        return 5;
    } catch (std.list::push_error<Named> e) {
        move e as void;
        return 6;
    }
    try {
        fail(&a, word);
        return 7;
    } catch (Bad e) {
        drop e;
    } catch (std.array::push_error<i32*> e) {
        move e as void;
        return 8;
    } catch (std.list::push_error<Named> e) {
        move e as void;
        return 9;
    }
    i32 next = await tick(a + b);
    return next - 13;
}

async i32 main() {
    try {
        return await work();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
