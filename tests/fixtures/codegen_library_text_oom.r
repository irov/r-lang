module test.codegen.library_text_oom;

import std.encoding;
import std.regex;
import std.string;
import std.text;

// M19: the wrapper fails each allocation in turn; every allocating operation of std.text,
// std.encoding, the R part of std.string and std.regex captures reports std.alloc::alloc_error
// and leaves nothing behind.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

i32 main() {
    try {
        try {
            str[3] words = {"alpha", "beta", "gamma"};
            std.string::string joined = std.text::join(words, ", ");
            std.string::string replaced = std.text::replace(joined, "a", "AA");
            std.string::string upper = std.text::ascii_uppercase(replaced);
            std.string::string lower = std.text::ascii_lowercase(upper);
            if (same(lower, "aalphaa, betaa, gaammaa") == false) {
                throw TestAssertionFailed {.code = 1};
            }
            std.string::string encoded = std.encoding::encode_base64(lower);
            std.string::string url = std.encoding::encode_base64_url(lower);
            bytes decoded = std.encoding::decode_base64(encoded);
            bytes url_decoded = std.encoding::decode_base64_url(url);
            if (std.bytes::equal(decoded.as_slice(), url_decoded.as_slice()) == false) {
                throw TestAssertionFailed {.code = 2};
            }
            std.string::string hex = std.encoding::encode_hex(decoded.as_slice());
            bytes unhex = std.encoding::decode_hex(hex);
            std.string::string percent = std.encoding::percent_encode("a b/é");
            bytes unpercent = std.encoding::percent_decode(percent);
            if (len(unhex) != len(decoded) || len(unpercent) != 6usize) {
                throw TestAssertionFailed {.code = 3};
            }
            std.string::insert_str(&lower, 0usize, "[");
            std.string::replace_range(&lower, 1usize, 3usize, "A");
            std.regex::regex pair = std.regex::compile("(\\w+)=(\\w+)?");
            o<array<o<std.regex::span>>> groups = std.regex::captures(&pair, "x key= y");
            switch (groups) {
            case variant o::some(found):
                if (len(*found) != 3usize) { throw TestAssertionFailed {.code = 4}; }
            case variant o::none:
                throw TestAssertionFailed {.code = 5};
            }
            return 0;
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 99};
        } catch (std.convert::parse_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 97};
        } catch (std.string::boundary_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 96};
        } catch (std.regex::error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 98};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
