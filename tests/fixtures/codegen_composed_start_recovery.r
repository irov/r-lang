module test.codegen.composed_start_recovery;

error Rejected {};
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }

Owner create(i32 value) { return Owner {.value = new i32(value)}; }
Owner transfer(Owner value) { return move value; }
i32 rejected() throws Rejected { throw Rejected {}; }
async i32 consume(Owner owner, i32 addition) { return *owner.value + addition; }

@generic<T: copy & send & unborrowed>
async T identity(T value, Owner owner) { return move value; }

async i32 main() {
    i32 failures = 0;
    try { return await consume(create(1), 0); }
    catch (std.async::start_error failure) { failures += 1; }

    Owner saved = create(42);
    try { return await consume(move saved, rejected()); }
    catch (Rejected failure) {
        if (*saved.value != 42) { return 2; }
        failures += 1;
    } catch (std.async::start_error failure) { return 3; }

    try {
        if (await consume(move saved, 20) != 62) { return 4; }
    } catch (std.async::start_error failure) { return 5; }

    Owner transferred = create(22);
    try { return await consume(transfer(move transferred), 20); }
    catch (std.async::start_error failure) { failures += 1; }

    try {
        i32 result = await identity(42, create(2));
        i32 selected = result == 42 && failures == 3 ? 0 : 6;
        return selected;
    } catch (std.async::start_error failure) { return 7; }
}
