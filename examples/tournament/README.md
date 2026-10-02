# Tournament planner

Generate a round-robin schedule, check registration, and rank numeric results.

```sh
ctest --test-dir build-debug -R 'example_tournament|tournament_commands' --output-on-failure
build-debug/tests/codegen_example_tournament schedule 4
build-debug/tests/codegen_example_tournament draw 8 3
build-debug/tests/codegen_example_tournament rank 2 10 4 10 -1
build-debug/tests/codegen_example_tournament rounds 5
build-debug/tests/codegen_example_tournament rounds 6 3
build-debug/tests/codegen_example_tournament table ann:3 bob:7 cid:3 dan:10
```

`schedule` accepts 2–32 players. Nested range comprehensions produce each pairing
once, while dictionary comprehensions count appearances. Literal collections hold
opponents and award rules; membership checks prevent unregistered pairings. The
final report lists match counts for each player in registration order. Conditional
`throw ... else ...` selects the appropriate explanation for an invalid size.

`draw` uses a user-defined `Draw` trait with an associated `Entry` type and an
associated constant `CAPACITY` that limits a draw to 32 players. A generic function
draws `Player` values; their `std.cmp::Equal` implementation, derived by `@derive(equal)`,
supports a registration search over a shared slice.

`rank` takes a bonus and scores in [-1000000, 1000000]. A closure owns its bonus,
and a generic callable constraint applies that scoring policy. A mutable slice
provides `swap` and `sift_down` to implement a max-priority queue in place. The
report is descending; duplicate scores remain separate results. A variadic total
receives the final slice through a spread argument. The completed round removes
one record, clears the rest, and reports that the reserved array capacity remains
available for reuse. No borrowing survives the container mutations.

`rounds` accepts 2–32 players and an optional first player. The circle method keeps the first
seat and rotates the others: `std.slice::rotate_left` moves the chosen first player into the
fixed seat, and `std.slice::rotate_right` turns the circle one seat after every round. An odd
field adds a bye seat that meets nobody. `core::swap` alternates the home side of the fixed
seat's game, so it does not host every round. Each pair of players meets exactly once: every
game becomes a `Pairing`, the lower number first, whose `@derive(equal, ordered, key)` makes it
an element of a `std.set::set`. The last line reports the distinct pairings, how many repeated
and the highest pairing in the derived order.

`table` ranks `NAME:POINTS` entries, most points first and equal points by name. Each row owns
its name, so the rows are not Copy: `std.slice::sort_by` exchanges them in place with a
comparison function. Before sorting, `core::clone` keeps the registration order; the clone of
each row calls the `Standing::clone` hook, which clones the owned name. Every line reports the
entry position a player had in that registered copy, found by a generic search over any
`std.cmp::Equal` type; `Standing` derives its `Equal`, which compares the owned names through the
`std.cmp` implementation for `std.string::string`.

