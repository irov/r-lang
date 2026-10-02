module test.codegen.generic_error_sets;

struct TransactionState { bool committed; };
error A { i32 x; }; error B { i32 x; };
i32 pure(i32 x) { return x; }
i32 one(i32 x) throws A { if (x < 0) { throw A {.x=x}; } return x; }
i32 two(i32 x) throws A, B { if (x == -1) { throw A {.x=x}; } if (x < 0) { throw B {.x=x}; } return x; }
@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 invoke(F f, i32 x) throws E { return f(x); }
@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 recover(F f, i32 x) throws E {
 own i32* keep = new i32(42);
 try { return f(x); }
 catch (A failure) { return *keep + failure.x; }
 finally { *keep = 9; }
}
@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 relay(F f, i32 x) throws E { return invoke(move f,x); }
@generic<E: errors & unborrowed, G: errors & unborrowed,
         F: fn(i32) -> i32 throws(E), H: fn(i32) -> i32 throws(G)>
i32 compose(F first, H second, i32 x) throws E, G {
 i32 intermediate = first(x);
 return second(intermediate);
}
@generic<E: errors & unborrowed, F: fn mut(i32) -> i32 throws(E)>
i32 retry(F f, i32 x) throws E {
 try { return f(x); } catch (A failure) { failure as void; }
 return f(x);
}
@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
void map(const i32[] source, i32[] destination, F operation) throws E {
 for (usize index = 0usize; index < len(source); index += 1usize) {
  destination[index] = operation(source[index]);
 }
}
@generic<E: errors & unborrowed, F: fn mut(i32*) -> i32 throws(E)>
i32 transaction(i32* state, F operation) throws E {
 i32 original = *state;
 TransactionState outcome = {.committed = false};
 try {
  i32 result = operation(state);
  outcome.committed = true;
  return result;
 } finally {
  if (outcome.committed == false) { *state = original; }
 }
}
i32 commit_state(i32* state) { *state += 1; return *state; }
i32 fail_state(i32* state) throws A { *state = -1; throw A {.x=7}; }
i32 fail_state_twice(i32* state) throws A, B {
 *state += 1;
 if (*state == 43) { throw B {.x=8}; }
 throw A {.x=9};
}
i32 main() {
    try {
     if (invoke(pure, 42) != 42) { throw TestAssertionFailed {.code = 1}; }
     try { invoke(one, -1) as void; throw TestAssertionFailed {.code = 2}; } catch (A failure) { if (failure.x != -1) { throw TestAssertionFailed {.code = 3}; } }
     try { relay(two, -2) as void; throw TestAssertionFailed {.code = 4}; } catch (A failure) { failure as void; throw TestAssertionFailed {.code = 5}; } catch (B failure) { if (failure.x != -2) { throw TestAssertionFailed {.code = 6}; } }
     try { if (recover(two, -1) != 41) { throw TestAssertionFailed {.code = 7}; } }
     catch (A failure) { failure as void; throw TestAssertionFailed {.code = 8}; } catch (B failure) { failure as void; throw TestAssertionFailed {.code = 9}; }
     try { if (compose(one, two, 42) != 42) { throw TestAssertionFailed {.code = 10}; } }
     catch (A failure) { failure as void; throw TestAssertionFailed {.code = 11}; } catch (B failure) { failure as void; throw TestAssertionFailed {.code = 12}; }
     try { compose(pure, two, -2) as void; throw TestAssertionFailed {.code = 13}; }
     catch (A failure) { failure as void; throw TestAssertionFailed {.code = 14}; } catch (B failure) { if (failure.x != -2) { throw TestAssertionFailed {.code = 15}; } }
     own i32* attempts = new i32(0);
     fn mut i32 transient(i32 x) move(attempts) throws A {
      *attempts += 1;
      if (*attempts == 1) { throw A {.x=x}; }
      return x;
     }
     try { if (retry(move transient, 42) != 42) { throw TestAssertionFailed {.code = 16}; } }
     catch (A failure) { failure as void; throw TestAssertionFailed {.code = 17}; }
     i32[2] source = {20, 22};
     i32[2] destination = {0, 0};
     i32[] destination_view = destination[0usize..2usize];
     map(source, destination_view, pure);
     if (destination[0] + destination[1] != 42) { throw TestAssertionFailed {.code = 18}; }
     i32 state = 41;
     if (transaction(&state, commit_state) != 42 || state != 42) { throw TestAssertionFailed {.code = 19}; }
     try { transaction(&state, fail_state) as void; throw TestAssertionFailed {.code = 20}; }
     catch (A failure) { if (failure.x != 7 || state != 42) { throw TestAssertionFailed {.code = 21}; } }
     try { transaction(&state, fail_state_twice) as void; throw TestAssertionFailed {.code = 22}; }
     catch (A failure) { failure as void; throw TestAssertionFailed {.code = 23}; }
     catch (B failure) { if (failure.x != 8 || state != 42) { throw TestAssertionFailed {.code = 24}; } }
     return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

