module test.regression.unused_constant_string;

/* M25-2: a constant module object that no reached code uses is not emitted, and the program
   strings of its initializer are not emitted either (an unused static C array is an error of
   the generated-C options). */
const constexpr str UNUSED = "never read";
const constexpr str USED = "read";

protected usize size_of_text(const u8[] data) {
    return len(data);
}

i32 main() {
    usize size = size_of_text(USED);
    return (size as i32) - 4;
}
