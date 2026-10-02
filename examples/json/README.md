# JSON

[main.r](main.r) reads a configuration, writes it back, and modifies a JSON tree.
The example runs in an async function. In-memory operations are synchronous and do not require `await`.

The field type determines storage. `@json(optional)` allows an input key to be absent;
`o<T>` stores `none` or `some` and accepts JSON `null`. These rules are independent.

```r
@json(optional, omitempty)
std.string::string email;
```

A missing `email` becomes an empty owning string. An empty string is omitted
when writing. An input `null` for this field remains an error.

The example also demonstrates `name`, `case`, `string`, `default`, `skip`, `omitzero`,
and `omitnone`, while `embed` flattens `Address.city` into the parent object.
`omitempty` checks the JSON representation and preserves `0`/`false`;
`omitzero` checks the original value and can omit them.

`unmarshal` obtains its type from the declaration or simple assignment. It borrows
the input bytes and returns an independent owner; the previous value is replaced
only after successful decoding. `marshal` borrows the value and returns
`std.string::string`. Parsing and allocation failures are checked errors.

`find`/`get` return `o<const std.json::value*>`. The borrow is limited by the tree's
lifetime. In this example, the scope containing the lookup result ends before the
tree is modified; `take_field` returns an independent owner of the removed value.

```sh
ctest --test-dir build-debug -R 'json_example' --output-on-failure
```

[hooks.r](hooks.r) defines independent `json_marshal`/`json_unmarshal` hooks for `UserId`,
combines them with `string`, and uses `json_is_zero` before conversion.

[stream.r](stream.r) feeds three-byte fragments, including split escape sequences.
The decoder yields root-array elements one at a time; the previous element is
released before the next. The size limit applies to each element, not the entire array.

[reader.r](reader.r) reads a sequence from stdin using direct `await`, then
returns the handle and unread bytes through `detach`, `take_handle`, and `take_bytes`.
Example input:

```json
{"id":"18446744073709551615","Display-Name":"Madrid"} -42 "tail"
```

Files and TCP use the same API with `reader<std.fs::file>` or
`reader<std.net::tcp_stream>`. The constructor accepts an existing handle through `move`;
allocation failure preserves the original owner. `none` from `read_next` means
clean EOF, including an empty stream. A parse error or cancellation of a started
operation prevents further JSON reads from this reader. Buffered bytes and the handle
can be recovered through `detach`.

All four examples run in CTest:

```sh
ctest --test-dir build-debug -R 'json_.*example' --output-on-failure
```
