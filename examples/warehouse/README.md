# Warehouse inventory

Update quantities for numeric SKU identifiers and inspect the resulting inventory. Commands
run in order, and quantities must remain nonnegative and fit in i32. The example exercises the
complete `std.dict` operation set with borrowed lookups, exclusive updates, replacement values,
ordered iteration and checked insertion errors.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_warehouse --output-on-failure
build-debug/tests/codegen_example_warehouse set 101 8 set 102 4 add 101 -3 show
build-debug/tests/codegen_example_warehouse set 101 8 get 101 remove 101 has 101
```

Commands:

- `set SKU QUANTITY`, `add SKU DELTA`
- `get SKU`, `has SKU`, `remove SKU`
- `reserve ADDITIONAL`, `show`, `clear`, `reset`

`set` prints the replaced quantity or `none`. `add` checks arithmetic and available stock before
updating the entry. `show` prints entries in storage order. `clear` reuses the dictionary's
allocation; `reset` replaces it with a fresh empty dictionary. A script stops at its first error.
[stock.r](src/stock.r) holds inventory behavior; [main.r](src/main.r) handles the CLI and output.
