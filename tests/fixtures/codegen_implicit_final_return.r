module test.codegen.implicit_final_return;

error issue {
    i32 code;
};

protected void plain_void() {
}

protected void sync_success() throws issue {
}

protected async void async_plain_void() {
}

protected async void async_success() throws issue {
}

async i32 main() {
    plain_void();
    try {
        sync_success();
    } catch (issue error) {
        error as void;
        return 1;
    }

    try {
        task<void> void_operation = async_plain_void();
        await move void_operation;
    } catch (std.async::start_error error) {
        error as void;
        return 5;
    }

    try {
        task<void throws issue> operation = async_success();
        await move operation;
        return 0;
    } catch (issue error) {
        error as void;
        return 2;
    } catch (std.async::start_error error) {
        error as void;
        return 4;
    }
}
