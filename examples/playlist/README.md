# Edit an ordered playlist

Apply a sequence of editing commands to a list of numeric track IDs. The tool prints each
operation's result; `show` prints the current order. Positions are zero-based, and missing
positions produce `missing` without changing the playlist.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_playlist --output-on-failure
build-debug/tests/codegen_example_playlist append 20 prepend 10 before 1 15 after 2 25 show
build-debug/tests/codegen_example_playlist append 10 append 20 set_first 11 remove 1 show
```

Commands:

- `append VALUE`, `prepend VALUE`, `set_first VALUE`, `set_last VALUE`
- `before INDEX VALUE`, `after INDEX VALUE`, `set INDEX VALUE`
- `get INDEX`, `remove INDEX`
- `first`, `last`, `pop_first`, `pop_last`, `clear`, `show`

[editor.r](src/editor.r) demonstrates every public `std.list` operation: stable node positions,
shared inspection, exclusive mutation, consuming removal, iteration and clearing. The
[command parser](src/main.r) uses a nominal enum and explicit switch fallthrough.
The runtime tests compare all commands and deterministic editing scripts with a Python list.
