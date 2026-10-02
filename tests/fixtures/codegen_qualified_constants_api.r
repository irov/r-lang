module test.codegen.qualified_constants_api;

// R-MOD-0002: exported constants of every value kind, read through qualified paths.
struct Limits { u32 low; u32 high; };
const Limits LIMITS = Limits { .low = 10u32, .high = 250u32 };
const u32[4] TABLE = {7u32, 2u32, 3u32, 4u32};
const bool FLAG = true;
const char MARK = 'r';
u32 span(const Limits* limits) { return limits->high - limits->low; }
