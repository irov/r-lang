# Logbook controls

Inspect logging levels, advance a threshold, filter messages or interpret a JSON
control event. Reflection drives names and ordering without runtime type metadata.

```sh
ctest --test-dir build-debug -R 'example_logbook|logbook_commands' --output-on-failure
build-debug/tests/codegen_example_logbook levels
build-debug/tests/codegen_example_logbook next warn
build-debug/tests/codegen_example_logbook filter info debug 'packet bytes' warn 'disk almost full'
build-debug/tests/codegen_example_logbook event info '{"Message":{"level":"warn","text":"disk almost full"}}'
build-debug/tests/codegen_example_logbook schema
```

Levels have explicit numeric discriminants, while filtering and `next` use their
declaration ordinals. `levels` enumerates the declarations and reports the count,
minimum and maximum. Advancing the highest threshold leaves it unchanged.

`filter` accepts level/message pairs and reports the accepted count. Empty and
Unicode messages are preserved. `event` decodes an owning payload enum from JSON:
`"Quit"`, `{"Threshold":"warn"}` or the `Message` object shown above. It reports
the active variant and applies the same threshold policy to a message event.

`schema` reports the compiler target/profile, settings type, its field names/count,
and the number of event variants. Field-index reflection is evaluated at compile
time; enum names and active payload-variant names can be selected at runtime.

The behavior checks compare every threshold/level pair, all enum alternatives,
invalid names and JSON, field metadata, empty batches and Unicode messages.
