module test.codegen.module_strings;

/* R-EXPR-0032, R-FUNC-0023: module string constants designate program-image bytes, whether
   spelled as literals or computed at translation. */
const str GREETING = "hello";
const constexpr str TITLE = "title";
const str EMPTY = "";
str pick(bool first) {
    if (first == true) { return "first"; }
    return "second";
}
const str CHOSEN = pick(false);

i32 main() {
    str greeting = GREETING;
    if (len(greeting) != 5usize) { return 1; }
    if (len(TITLE) != 5usize) { return 2; }
    if (len(EMPTY) != 0usize) { return 3; }
    str chosen = CHOSEN;
    if (len(chosen) != 6usize) { return 4; }
    return 0;
}
