module codegen.async_string_views;

protected async usize byte_length() {
    constexpr str text = "async-view";
    str runtime_view = text;
    const u8[] bytes = runtime_view;
    usize length = len(bytes);
    return length;
}

async i32 main() {
    try {
        task<usize> operation = byte_length();
        usize length = await move operation;
        if (length != 10) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 1;
    }
}
