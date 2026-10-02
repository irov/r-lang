# Shared snapshots and weak subscriptions

Publish a versioned snapshot, let readers share it, refresh a weak subscription, and try to
recover the value when publication ends. Select `rc` for local sharing or `arc` for atomic
reference counting; `shared` retains a reader and `unique` releases it before recovery.

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j8
ctest --test-dir build-debug -R example_snapshots --output-on-failure
build-debug/tests/codegen_example_snapshots rc unique 7 11
build-debug/tests/codegen_example_snapshots arc shared 7 11
```

Both runs publish version 2 with value 11. The unique run unwraps the value and the subscription
expires. The shared run returns the remaining owner; its reader keeps the snapshot alive.
The output includes reference counts and whether readers refer to the same allocation.

[rc.r](src/rc.r) and [arc.r](src/arc.r) show the complete APIs under identical policies.
Each passes one cloned reference through an opaque raw token and recovers that token exactly
once in an `unsafe` block. The example does not dereference or manufacture a raw address.
This models the ownership boundary for an interface that stores opaque tokens; it does not
call an external C library. See [C callbacks](../c_callbacks/README.md) for an actual C callback.

The local `rc` scenario completes synchronously before async output, so no local reference
crosses an `await`. An optional reader keeps ownership initialized on both conditional paths.

`core::replace` publishes the new record while returning the previous revision for the audit
line. A `Delivery` stores the upgraded subscription; `core::take(&delivery.pending)` extracts
that one-shot delivery and leaves `none`, so the containing record remains initialized.
Replacement uses exclusive access and does not bypass another reader.
