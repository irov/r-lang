# JSON workbench

A command-line tool for configuration files, JSON editing and incremental reading. Build and
check it with `ctest --test-dir build-debug -R 'example_json_tool' --output-on-failure`.
Use `build-debug/tests/codegen_example_json_tool` in place of `json_tool` below.

```sh
json_tool pretty '{"tasks":[true,null,42]}' 2
json_tool set '{"enabled":false}' enabled true
json_tool take '{"name":"Ada","active":true}' name
json_tool index '[10,20,30]' 1
json_tool shift '[10,20,30]' 1
json_tool stats '{"items":["Madrid",true,null,42]}'
json_tool number '18446744073709551615'
json_tool config '{"Display-Name":"Ada","city":"Madrid"}'
json_tool fragments '[{"name":"Ada"},{"name":"Madrid"}]' 3
printf '%s' '{"id":42} remaining bytes' | json_tool first
```

Commands also include `compact`, `keys`, `get`, `append`, `names LEFT RIGHT`, `strict`,
`snapshot` and `reload BASE UPDATE`. Run without arguments for their complete syntax.
Missing `get`/`index` results print JSON null. `take` and `shift` report both the removed
value and the remaining document. `stats` destroys an owned traversal worklist instead of
using recursive R calls; its stack depth does not grow with document nesting.

## Configuration contract

`config` decodes a `Configuration` and writes its normalized representation with two-space
indentation. `strict` also rejects unknown fields. The generic `canonical` function mentions
its type parameter only in its body, so `normalize` names it explicitly:
`canonical::<Configuration>(source, options)`. The configuration demonstrates every
JSON field parameter:

| Field | Contract |
|---|---|
| `id` | `optional`, factory `default`, `string`, `omitzero`; a custom `UserId` type with independent encode/decode hooks and `json_is_zero` |
| `name` | `name = "display_name"`, `case = "ignore"` |
| `address` | `embed`; its `city` key is required in the parent object |
| `email` | `optional`, `omitempty`; absence creates an empty owning string, null is rejected |
| `retries` | `optional`, literal `default = 3` |
| `verbose` | `optional`, `omitzero` |
| `token` | `optional`, `omitnone`; an optional owning string, where null is distinct from an empty string |
| `origin` | `skip`, string `default = "local"`; excluded in both directions |

`names` exposes exact and Unicode-folded name matching. `reload` reports whether the replacement
succeeded and prints the resulting configuration: a failed replacement preserves the old owner.
`snapshot` accepts `{"configuration": ...}` through a generic schema constrained by
`json_encode & json_decode`.

## Streaming and precision

`fragments ARRAY CHUNK_SIZE` emits one compact line per root-array element. It exercises
incremental tokenization, including splits inside UTF-8 and escape sequences. The command-line
input and printed report are held in memory; the decoder does not build the full input tree.

`first` reads one document from stdin, detaches the reader, recovers both its buffered suffix
and the input handle, then reports the length and CRC32 of the complete remaining byte stream.
The suffix need not be UTF-8 or JSON. This command collects the suffix in memory.

`number` prints the original checked number spelling, its mathematical zero classification and
the number itself. It preserves integers beyond floating-point precision and extreme exponent
spellings. No floating conversion is used to store an exact JSON number.

Errors report the byte offset and JSON Pointer. Exit codes: 64 usage, 65 invalid JSON/numeric
input, 71 when a JSON array cannot grow. Allocation, I/O and async-launch failures reach the
implicit `main` error boundary (112, 113 and 116). Positive and negative commands are checked against
Python JSON, Decimal and CRC32 results; fragment boundaries and stdin handoff are exercised too.
