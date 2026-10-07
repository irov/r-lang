module test.codegen.core_format_failures;

/* R-TYPE-0046 (L32): every allocation of formatting may fail. A failure inside an
   implementation, inside standard formatting and while a slot is rendered throws
   std.alloc::alloc_error, destroys the partial text of a rendered slot and leaves the text
   appended before it in an explicit builder, which its owner then destroys. */

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = *source;
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

@derive(format)
struct Size { u32 width; u32 height; };

struct Label { std.string::string text; };
impl core::Format for Label {
    void format(const Label* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string copy = f"<{this->text}>";
        std.format::append_str(out, copy);
    }
};

std.string::string run() throws std.alloc::alloc_error {
    Label label = Label {.text = std.string::from_str("name")};
    Size size = Size {.width = 1u32, .height = 2u32};
    o<Label> maybe = o::some(move label);
    std.string::string first = f"{size} {maybe:14}";
    std.format::builder out = std.format::create();
    maybe.format(&out);
    size.format(&out);
    std.string::string second = std.format::finish(move out);
    return f"{first}|{second}";
}

i32 main() {
    try {
        std.string::string text = run();
        if (matches(&text,
                    "Size { width: 1, height: 2 }   some(<name>)|some(<name>)Size { width: 1, height: 2 }") ==
            false) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failed) {
        return 99;
    }
}
