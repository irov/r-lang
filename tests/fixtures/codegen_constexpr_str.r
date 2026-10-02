module codegen.constexpr_str;

struct TextHolder {
    constexpr str text;
};

protected constexpr str identity(constexpr str value) {
    return value;
}

i32 main() {
    constexpr str joined = identity("left" "right");
    TextHolder holder = {
        .text = joined,
    };
    TextHolder empty = {};
    str runtime_view = joined;
    const u8[] bytes = runtime_view;
    usize joined_length = len(holder.text);
    usize empty_length = len(empty.text);
    usize byte_length = len(bytes);
    if (joined_length == byte_length) {
        if (empty_length == 0) {
            return 0;
        } else {
            return 1;
        }
    } else {
        return 1;
    }
}
