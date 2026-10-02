# Text toolkit

Everyday text and byte work without hand-written loops over bytes: `std.text` (Library
R-SLIB-TEXT-0002..0004) splits, trims, numbers lines, walks scalars and folds ASCII case,
`std.encoding` (R-SLIB-ENCODING-0001..0004) converts bytes to and from base64, hexadecimal and
percent-encoding, the R parts of `std.bytes` and `std.string` (R-SLIB-BYTES-0009,
R-SLIB-STRING-0004) pack integers into byte records and edit strings at scalar boundaries,
`std.iter` (R-SLIB-ITER-0004..0005) filters, reverses, chunks, windows and reduces, and
`std.regex` (R-SLIB-REGEX-0007) reports capturing groups.

```sh
ctest --test-dir build-debug -R 'example_textkit' --output-on-failure
build-debug/tests/codegen_example_textkit fields 'a, b ,, c' ,
build-debug/tests/codegen_example_textkit encode 'hi é?'
build-debug/tests/codegen_example_textkit stats 5 -1 4 -1 5 9 2
build-debug/tests/codegen_example_textkit groups '(\d+)-(\d+)(?:-(\d+))?' 'on 2026-09 ok'
```

`textkit fields` prints every piece between the separators with the whitespace that
`trim_start` and `trim_end` would remove. The pieces borrow from the argument, so nothing is
copied until a row is formatted:

```r
for (str piece in std.text::split(line, separator)) {
    str trimmed = std.text::trim(piece);
    usize lead = size_of_text(piece) - size_of_text(std.text::trim_start(piece));
    ...
}
```

`textkit decode` reports the first error of a text that is not canonical, such as a byte outside
the alphabet or an incomplete last group, with its byte index (`std.convert::parse_error`):

```text
$ textkit decode base64 Zm9vY
textkit: trailing_character at byte 4
```

`textkit pack` writes each number in the fewest of 1, 2, 4 or 8 bytes into a 32-byte record,
little-endian at even and big-endian at odd positions, with a `std.bytes::cursor`, then reads the
values back with a second cursor; a number that no longer fits reports `out_of_bounds` and
changes nothing.

`textkit stats` counts how often the greatest number occurs with a generic function whose header
constrains the items of its iterator (Core R-TYPE-0043), so the body may compare and copy them:

```r
@generic<I: core::Iterator, I::Item: std.cmp::Ordered & copy>
usize count_of_greatest(I inner) { ... }
```

`textkit groups` prints the match that search selects and each capturing group: the preferred
path among those matching that span decides, a repeated group reports its last iteration, and a
group that takes no part is `none`:

```text
$ textkit groups '(a|b)+' xxabba 1
1 groups
0: 2..6 [abba]
1: 5..6 [a]
```
