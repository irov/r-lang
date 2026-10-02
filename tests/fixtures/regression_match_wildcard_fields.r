module test.regression.match_wildcard_fields;

/* M27-2: a named-field pattern that leaves two or more fields to wildcards declared one hidden
   local name twice and was rejected as a duplicate local declaration. */

struct three { std.string::string source; u32 a; u32 b; };

u32 wild(three value) {
    std.string::string taken = match (move value) { case { .source = move kept, .a = _, .b = _ }: move kept; };
    return std.string::len(&taken) as u32;
}

@generic<R: unborrowed>
struct holder { R source; bytes buffer; usize start; usize end; };

@generic<R: unborrowed>
(R, bytes) holder<R>::into_parts(holder<R> this) throws std.alloc::alloc_error {
    bytes rest = {};
    std.bytes::append(&rest, this.buffer[this.start..this.end]);
    return match (move this) { case { .source = move taken }: (move taken, move rest); };
}

i32 main() {
    try {
        bytes data = {};
        std.bytes::append(&data, "hello world");
        holder<std.string::string> item = {.source = std.string::from_str("abc"), .buffer = move data,
                                           .start = 6usize, .end = 11usize};
        (std.string::string, bytes) pieces = (move item).into_parts();
        u32 extra = wild(three {.source = std.string::from_str("xy"), .a = 1u32, .b = 2u32});
        return match (move pieces) {
            case { .0 = move name, .1 = move rest }: (std.string::len(&name) + len(rest)) as i32 + extra as i32 - 10;
        };
    } catch (std.alloc::alloc_error failed) {
        failed as void;
    }
    return 1;
}
