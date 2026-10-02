module test.codegen.async_fused_default_own;

struct Scratch {
    u8[1_048_576] bytes;
};

protected async i32 ready() {
    return 7;
}

async i32 main() {
    own Scratch* scratch = new Scratch{};
    try {
        task<i32> operation = ready();
        i32 value = await move operation;
        if (value != 7) {
            return 1;
        }
        value as void;
        if (scratch->bytes[0] != 0) {
            return 2;
        }
        if (scratch->bytes[1_048_575] != 0) {
            return 3;
        }
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 4;
    }
}
