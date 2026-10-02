# Search, redact and inspect text

A command-line text workbench with compiled regular expressions, byte offsets, owned
replacement results and checked UTF-8 editing. It reads command arguments and writes the
result to stdout. Run without arguments for the complete command syntax.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_text --output-on-failure
build-debug/tests/codegen_example_text all '\d+' 'item=42 count=7'
build-debug/tests/codegen_example_text replace '\d+' 'item=42 count=7' '#'
build-debug/tests/codegen_example_text split '[,;]\s*' 'one, two;three'
build-debug/tests/codegen_example_text check '[a-z]+' hello
build-debug/tests/codegen_example_text inspect 'WARN: retry' WARN
build-debug/tests/codegen_example_text message INFO ready 100
```

Search rows contain `start:end`, a tab, and the matched text. Offsets count UTF-8 bytes.
`find-i` enables ASCII case-insensitive matching; searches use multiline anchors.
`from` starts at an explicit byte offset. `check` prints both partial and full-match results.
`escape` quotes a literal for use as a pattern; `replace` uses a literal replacement.
`split` uses default regex options and preserves empty fields.

`inspect` reports byte-level text properties and an ASCII-lowercased copy. `message`
assembles `PREFIX: BODY`, optionally truncates it, and prints byte length and scratch-buffer
capacity. The capacity is implementation-dependent. A limit inside a UTF-8 scalar is rejected.

The source is split into [search.r](src/search.r), [inspect.r](src/inspect.r) and
[main.r](src/main.r). End-to-end checks cover every command, empty matches, Unicode offsets,
rejected patterns and invalid UTF-8 boundaries.
