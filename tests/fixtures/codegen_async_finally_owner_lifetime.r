module test.codegen.async_finally_owner_lifetime;

protected void increment(i32* value) {
    *value += 1;
}

protected async i32 owner_lifetime() {
    own i32* value = new i32(83);
    try {
        return 9;
    } finally {
        increment(&*value);
    }
}

async i32 main() {
    try {
        task<i32> operation = owner_lifetime();
        i32 value = await move operation;
        if (value != 9) {
            return 1;
        }
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
    return 0;
}
