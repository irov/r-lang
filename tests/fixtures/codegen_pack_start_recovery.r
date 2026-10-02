module test.codegen.pack_start_recovery;

/* R-TYPE-0053, R-FUNC-0010 (L18.4): the pieces of a spread are staged Move operands of an async
   start; a rejected start destroys them exactly once, whether a tuple is partitioned now or a
   pack when its definition is instantiated. After the start of main, the wrapper rejects the
   first spread start, the start inside prepend and the start inside invoke, and counts the
   releases of every owner. */
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }

Owner create(i32 value) { return Owner {.value = new i32(value)}; }

async i32 consume(Owner first, i32 addition, Owner second) {
    return *first.value + addition + *second.value;
}

@generic<T...: send & unborrowed>
async usize count(T... values) {
    move values as void;
    return len(T...);
}

@generic<T...: send & unborrowed>
async usize prepend(T... values) {
    try {
        return await count(create(7), ...move values);
    } catch (std.async::start_error failure) {
        failure as void;
        return 100usize;
    }
}

@generic<F: async fn once(P...) -> R, R: send & unborrowed, P...: send & unborrowed>
async R invoke(F operation, P... args) throws std.async::start_error {
    return await (move operation).call(...move args);
}

async i32 main() {
    i32 failures = 0;
    (Owner, i32, Owner) first = (create(1), 2, create(3));
    try {
        return await consume(...move first);
    } catch (std.async::start_error failure) {
        failure as void;
        failures += 1;
    }

    (Owner, i32, Owner) second = (create(10), 20, create(30));
    try {
        if (await consume(...move second) != 60) { return 2; }
    } catch (std.async::start_error failure) {
        failure as void;
        return 3;
    }

    try {
        if (await prepend(create(4), create(5)) != 100usize) { return 4; }
    } catch (std.async::start_error failure) {
        failure as void;
        return 5;
    }

    auto operation = consume;
    try {
        return await invoke(operation, create(40), 1, create(50));
    } catch (std.async::start_error failure) {
        failure as void;
        failures += 1;
    }
    i32 selected = failures == 2 ? 0 : 6;
    return selected;
}
