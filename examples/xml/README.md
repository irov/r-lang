# Stream, select and rewrite XML

A command-line filter over `std.xml`. It reads a document from stdin, runs the streaming
reader in chunks of a chosen size and prints the event stream, the elements selected by a
streaming path pattern, or the document rewritten through the escaping writer. Run without
arguments for the complete command syntax.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_xml --output-on-failure
printf "<root a='1'><item id='2'>alpha &amp; beta</item><empty/></root>" | build-debug/tests/codegen_example_xml events 7
printf "<root><item id='2'/><item id='3'/></root>" | build-debug/tests/codegen_example_xml select "/root/item[@id='3']"
printf "<a><b>t</b><c/></a>" | build-debug/tests/codegen_example_xml pretty
```

`events [CHUNK]` prints one line per event: the kind, the depth after the event, the name,
the text and every attribute as `name=value`, with tabs between fields and tab, LF, CR and
backslash spelled as escapes. `events_all` keeps whitespace-only text. `select PATTERN
[CHUNK]` compiles the streaming subset of a location path (`/root/item[@id='3']`,
`//item`, `item/*`, `[@attr]`) and prints the ordinal, depth, path, local name, namespace
URI and attributes of every element on that path. `compact` and `pretty` rewrite the
document through `std.xml::writer` with a fresh declaration and without whitespace text.

The reader takes UTF-8 only, decodes the five predefined entities and character references,
resolves namespace prefixes and rejects DOCTYPE, other entities and other encodings as the
checked error `unsupported`; malformed, unbalanced and over-limit documents and invalid
selectors exit with status 65 and a message naming the error code and byte offset.

`tests/run_xml_examples.py` is a differential test: the event stream of generated documents,
fed in pieces of every size, is compared with the events that Python's expat reports, the
selector against an independent evaluation over ElementTree, and both rewrites parse back to
the same events.
