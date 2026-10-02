# Nullable pointers

This example shows nullable borrows and owners, `null`, flow-sensitive dereference checks, nullable borrow returns, generic borrows into caller-owned storage, and default initialization of nullable pointer fields.

```sh
r-front --emit=c17 examples/pointers/main.r
```

A safe nullable borrow or owner can be dereferenced only on a path where comparison with `null` proves that it is present. Returning `null` from a nullable borrow function does not attach the result to an input lifetime. An omitted nullable pointer field is initialized to `null`.

The generic `box_value`, `first`, and `first_slice` functions return borrows into storage supplied by the caller. A generic function cannot return the address of its by-value parameter or a slice of its local fixed array because that automatic storage is destroyed at return.
