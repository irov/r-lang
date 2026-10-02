# Key-value store with a configured backend

Store numeric values under numeric keys in one of two backends that the first argument
selects. Both backends implement one trait; all commands run through a `dyn(Store)*`
interface, so the command loop is compiled once for every backend.

```sh
ctest --test-dir build-debug -R 'example_keystore|keystore_commands' --output-on-failure
build-debug/tests/codegen_example_keystore slots put 1 10 put 2 20 get 1 count backend
build-debug/tests/codegen_example_keystore log put 7 1 put 7 2 remove 7 get 7 count
```

Commands: `put KEY VALUE`, `get KEY`, `remove KEY`, `count`, `backend`.

[store.r](src/store.r) declares the `Store` trait and runs the commands:

```r
trait Store {
    constexpr str name(const Self* this);
    o<u32> get(const Self* this, u32 key);
    void put(Self* this, u32 key, u32 value) throws Full, std.array::push_error<Record>;
    bool remove(Self* this, u32 key);
    usize count(const Self* this);
};

void run(dyn(Store)* backend, const Step[] steps, std.string::string* output) ...
```

[backends.r](src/backends.r) implements it twice. `Slots` keeps eight fixed slots without
allocating and refuses a ninth distinct key with `Full`; `Log` appends a record for every
`put` and retires the key's previous record. [main.r](src/main.r) parses the commands;
its `open` allocates the selected backend once and keeps it in a `Session` as
`own dyn(Store)*`, an owner whose member type is fixed only when the program runs (Core
R-TYPE-0055). `execute` passes `&session.backend` where `run` expects `dyn(Store)*`: the
borrow of the owner becomes a borrow of its member, and dropping the session drops the
backend once.

The compiler knows every type the program converts to `dyn(Store)`, so each interface call
is a `switch` over those types with a direct call to each implementation. Stack bounds,
checked errors and resource contracts are checked for every implementation a call can
reach (Core R-TYPE-0051). The interface borrows the backend exclusively for the duration of
`run`, exactly like `Slots*` would.
