module test.codegen.async_option_path;

protected async i32 zero() {
    return 0;
}

async i32 main() {
    constexpr str literal = "leaf";
    str text = literal;
    try {
        std.fs::path path = std.fs::path_from_utf8(text);
        o<std.fs::path> retained = o::some(move path);
        task<i32> operation = zero();
        i32 value = await move operation;
        o<std.fs::path> after = move retained;
        return value;
    } catch (std.fs::path_error error) {
        error as void;
        return 1;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
}
