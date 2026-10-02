# Concurrent sensor summary

Apply a calibration offset, then compute the sum and peak of sensor readings in two scoped tasks. Both tasks borrow the
same owned array without copying it. The group bounds outstanding observations to two.
`select` consumes whichever analysis finishes first and awaits the other inside its clause;
its `until` clause bounds the wait by a five-second budget on the monotonic clock. The array
remains available after the group closes.

```sh
ctest --test-dir build-debug -R 'r_(frontend_codegen_example_fanout|example_fanout_commands)' --output-on-failure
build-debug/tests/codegen_example_fanout 0 12 -3 21
# count=3 sum=30 peak=21
```

`@scoped` permits borrowed parameters because group exit waits for child cleanup, including
when the parent is cancelled or an error leaves the block. Borrowed storage must be declared
outside the group body. An unconsumed task is cancelled and drained on group exit; use
`await move name` to observe its result or checked completion error. Named tasks are useful
here because both computations start before either result is consumed.

A select never cancels the analysis it did not choose. When the budget runs out first, both
analyses stay unconsumed, the group exit cancels and drains them, and the program exits with
status 75; `group.cancel_all()` followed by `await group.all()` would do the same while
staying inside the group. A cancelled analysis produces no report.

The example does not promise a speedup for small inputs. It demonstrates bounded fan-out
and shared input lifetime without introducing transport or scheduling dependencies. Integer
sums use R's existing checked-overflow behavior; input parse errors exit with status 65.

The calibration factory returns an opaque callable contract. Its captured offset and concrete
closure type remain private; the caller can apply the policy without boxing or dynamic dispatch.
For example, offset `-2` converts raw readings `12 21` to calibrated readings `10 19`.
