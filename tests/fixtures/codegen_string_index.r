module test.codegen.string_index;

/* R-EXPR-0021: indexing a str or constexpr str reads a byte of its backing storage, in
   ordinary and async functions and through fields. */
struct Named { str text; };
u8 at(str text, usize index) { return text[index]; }
async u8 later(usize index) { constexpr str word = "read"; return word[index]; }
async i32 main() {
    str s = "hello";
    constexpr str c = "abc";
    Named n = {.text = "xyz"};
    if (s[1] != 101u8) { return 1; }
    if (c[2] != 99u8) { return 2; }
    if (at(s, 4usize) != 111u8) { return 3; }
    if (n.text[0] != 120u8) { return 4; }
    if (await later(0usize) != 114u8) { return 5; }
    return 0;
}
