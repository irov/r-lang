# Cached price quotes

Calculate tax-inclusive prices in integer minor units, or publish them while a
second thread reads snapshots. Tax is expressed in basis points (10000 = 100%).
Rounding is half up; price is limited to one trillion minor units.

```sh
ctest --test-dir build-debug -R 'example_quotes|quotes_commands' --output-on-failure
build-debug/tests/codegen_example_quotes prices 100 250 999
build-debug/tests/codegen_example_quotes rate 750 100 250 999
build-debug/tests/codegen_example_quotes watch 100 250 999
R_QUOTES_TAX=1500 build-debug/tests/codegen_example_quotes prices 250
```

`prices` reports each revision, its net/gross price and the previous gross price.
`watch` publishes all gross prices on a scoped native worker, reads concurrent
snapshots on the foreground thread, and reports the final revision/price. Its
snapshot count is intentionally nondeterministic. Both share a read/write-lock
implementation with nonblocking attempts and blocking fallbacks. Guard views end
before explicit unlock. The final read happens after joining the publisher.

Policy validation runs through `once`, and the numeric tax rate is initialized
lazily through `once_lock`. The default rate is 2000 basis points; `R_QUOTES_TAX`
overrides it. `rate` sets the cell explicitly before quoting and therefore does not
read that environment variable. A final `get` reports whether a rate was initialized.
`R_QUOTES_ENABLED`, when present, must equal `yes`; another value rejects quotes.
An empty batch does not run policy validation or lazy initialization.

`force` calculates the same quotes through `call_once_force`. This is the entry
point an unwind-capable host would use for a serial retry after a poisoned
initializer. The default abort runtime cannot recover from a process panic.
A checked validation error leaves the once state retryable.

The command tests use Python integer arithmetic as the oracle and exercise
concurrent publication, environment defaults, explicit rates, rounding, boundaries,
empty batches and rejected policies. They do not require a remote service.
