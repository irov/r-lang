module test.codegen.async_composed_calls;

error Rejected {};
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }

Owner create(i32 value) { return Owner {.value = new i32(value)}; }
async i32 checked(bool reject) throws Rejected {
    throw (reject == true) Rejected {};
    return 20;
}
i32 consume(Owner owner, i32 value) { return *owner.value + value; }
async i32 relay() throws Rejected, std.async::start_error { return await checked(false); }

async i32 main() {
    try {
        if (consume(create(22), await relay()) != 42) { return 1; }
        return consume(create(1), await checked(true));
    } catch (Rejected failure) {
        return 0;
    } catch (std.async::start_error failure) {
        return 2;
    }
}
