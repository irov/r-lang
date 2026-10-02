module test.codegen.qualified_constants;
import test.codegen.qualified_constants_api;

/* R-MOD-0002: a qualified module object is the same place as its unqualified name, in
   projections, borrows, conditions, copies and translation-time constants. */
const u32 HIGH = test.codegen.qualified_constants_api::LIMITS.high;

i32 main() {
    u32 low = test.codegen.qualified_constants_api::LIMITS.low;
    if (low != 10u32) { return 1; }
    u32 span = test.codegen.qualified_constants_api::span(
        &test.codegen.qualified_constants_api::LIMITS);
    if (span != 240u32) { return 2; }
    if (test.codegen.qualified_constants_api::FLAG == false) { return 3; }
    u32[4] copy = test.codegen.qualified_constants_api::TABLE;
    if (copy[0] != 7u32) { return 4; }
    if (test.codegen.qualified_constants_api::TABLE[1] != 2u32) { return 5; }
    if (HIGH != 250u32) { return 6; }
    if (test.codegen.qualified_constants_api::MARK != 'r') { return 7; }
    return 0;
}
