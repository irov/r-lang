module test.codegen.variadic_views;

/* R-FUNC-0018 (L25): a variadic parameter may pack shared views: runtime str, shared borrows and
   shared slices. A result that carries what the elements view borrows the packed arguments, or
   the elements of the spread operand; the hidden array itself never outlives the call. */

// The longest word, a view of one of the packed arguments.
str longest(str... words) {
    str best = "";
    for (str word in &words) {
        if (len(word) > len(best)) { best = word; }
    }
    return best;
}

usize total(const u8[]... parts) {
    usize sum = 0usize;
    for (auto part in &parts) { sum += len(*part); }
    return sum;
}

// The largest pointee, as a borrow of the argument that holds it.
const i32* largest(const i32*... values) {
    const i32* best = values[0];
    for (auto value in &values) {
        if (**value > *best) { best = *value; }
    }
    return best;
}

// The words after the first, forwarded as a spread of a range.
usize rest(str first, str... others) {
    first as void;
    return len(others);
}

i32 main() {
    i32 failures = 0;
    std.string::string owned = std.string::from_str("alphabet");
    str word = longest("a", owned, "abc");
    if (len(word) != 8usize) { failures += 1; }
    str none = longest();
    if (len(none) != 0usize) { failures += 1; }
    u8[3] a = {1u8, 2u8, 3u8};
    u8[2] b = {4u8, 5u8};
    if (total(a[..], b[..], a[1..]) != 7usize) { failures += 1; }
    i32 x = 3;
    i32 y = 9;
    i32 z = 4;
    const i32* top = largest(&x, &y, &z);
    if (*top != 9) { failures += 1; }
    str[4] words = {"zero", "one", "two", "three"};
    if (rest(words[0], ...words[1..]) != 3usize) { failures += 1; }
    str spread = longest(...words[..2]);
    if (len(spread) != 4usize) { failures += 1; }
    drop owned;
    return failures;
}
