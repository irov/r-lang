# Regular expressions

`std.regex` compiles reusable expressions and searches valid UTF-8 strings. Import the
module with `import std.regex;` and pass `--library-map library/r/library.map` to `r-front`.
The module is written in safe R and compiled with your program. It requires
the `hosted` profile and has no external regex dependency.

```r
std.regex::regex number = std.regex::compile("[0-9]+");
o<std.regex::span> found = std.regex::find(&number, "item=42");
bool valid = std.regex::full_match(&number, "42");
std.string::string text = std.regex::replace_all(&number, "item=42", "#");
```

These calls throw `std.regex::error` and `std.alloc::alloc_error`; see [main.r](main.r)
for a complete program with handlers. The compiled expression owns its program and
does not borrow its original pattern. Searches borrow the expression and subject.
Spans contain `start` and `end` byte offsets, always on Unicode scalar boundaries.
Replacement and split results own their strings. A compiled expression is Move,
Send+Sync and may be transferred into an async task; regex operations themselves
are synchronous.

## Syntax and selection

Supported: literals, `.`, `^`, `$`, alternatives, groups, `(?:...)`, classes and
ranges, negated classes, `*`, `+`, `?`, `{m}`, `{m,n}`, `{m,}`. Groups organize the
pattern and do not capture substrings. A search returns the earliest match and,
among matches starting there, the longest. Thus `a|ab` finds `ab` in `zab`.

Escapes include punctuation, `\n`, `\r`, `\t`, `\f`, `\v`, `\xHH`, ASCII
`\d`/`\D`, `\w`/`\W`, `\s`/`\S` and ASCII word boundaries `\b`/`\B`.
In an R string, escape the backslash: `"\\d+"`. `\xHH` denotes a Unicode scalar,
not an arbitrary raw UTF-8 byte. Positive class escapes work inside brackets;
negative class escapes and word-boundary escapes inside brackets are unsupported.

Backreferences, lookaround, named groups, Unicode properties, inline flags, lazy
and possessive repeats are rejected. There is no capture expansion in replacements:
`$1` and backslashes are literal text. Use `escape_literal` when constructing a
pattern from literal user text.

## Options and limits

```r
std.regex::options settings = std.regex::default_options();
settings.ignore_ascii_case = true;
settings.multiline = true;
settings.dot_all = true;
settings.max_steps = 1000000usize;
std.regex::regex expression = std.regex::compile_with_options("^item=.*$", settings);
```

The default flags are false and the default execution budget is 10 million steps.
Case folding changes ASCII letters only. Dot normally excludes LF. Anchors are
absolute unless multiline is enabled; `$` does not implicitly match before a final LF.

Compilation is limited to 16 KiB of pattern, 128 nested groups, repetition bounds
up to 1000, 4096 NFA instructions and 4096 class ranges. Execution uses an iterative
Thompson NFA with no backtracking.
Each search has its own scratch space. `find_all`, `replace_all` and `split` share
one step budget across their internal searches. Exceeding a limit throws a typed
error. Large replacement output can still require proportionally large memory.

## Empty matches and offsets

`none` means no match; `[k,k)` is a successful empty match. `find_all` advances by
one Unicode scalar after an empty match and returns EOF once. For an empty pattern
and `é`, spans are `[0,0)` and `[2,2)`. Empty matches adjacent to a nonempty match
are included. `split` retains empty pieces; `replace_all` inserts literal text at
empty spans. `find_from` accepts a byte offset on a scalar boundary and keeps the
original anchor and word-boundary context.

## Run the example and tests

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug --output-on-failure -R regex
```

The test suite compiles and runs this example, checks the same semantic cases in
sync and async programs, injects every allocation failure in a representative pipeline,
and runs a deterministic differential corpus against a substring-enumerating oracle.
