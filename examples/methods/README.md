# Methods, overloads, and traits

The program moves a group of shapes, totals their area, and calculates fencing
for circular and rectangular plots, then quotes their painting costs. `Point::shift` accepts either two coordinates
or a displacement point. The free `perimeter` function accepts either shape type
or `null` for a vacant plot, which needs no fence.
Both use the same overload rules: a unique exact parameter match wins; otherwise
there must be exactly one compatible signature. The result type does not choose
an overload.

`null_t` marks a parameter that accepts only the literal `null`:

```r
f64 perimeter(null_t vacant) { return 0.0; }
```

`perimeter(null)` selects this exact overload. A typed nullable pointer, even when
its value is null, selects a pointer overload instead. `null_t` cannot be used for
variables, fields, containers, or function results. The parameter name is a marker;
the body uses the literal `null` if it needs to pass absence to another function.

A method declares its receiver explicitly, as the first parameter named `this`:

```r
f64 Point::norm_squared(const Point* this) { ... }
void Point::shift(Point* this, f64 dx, f64 dy) { ... }
Point Point::origin() { ... }
```

`point.norm_squared()` and `view->norm_squared()` call the method with the receiver as its
first argument; the borrow kind comes from the declared receiver form. A declaration without
a receiver is an associated function and is called as `Point::origin()`. Without a call,
`Point::norm_squared` is a function item whose first parameter is the receiver:
`auto measure = Point::norm_squared;` is later called as `measure(&center)`.

A method marked `@chain` lets its call continue with further suffixes, which suits builders. Each
link below takes the order and returns it, and the chain ends with `cost`:

```r
@chain FenceOrder FenceOrder::with_height(FenceOrder this, f64 height) { ... }

f64 fence_cost = FenceOrder::around(14.0).with_height(1.5).with_gates(2u32).cost(10.0);
```

After a method without `@chain` the expression ends there; a parenthesized receiver, as in
`(order.with_height(1.5)).cost(10.0)`, remains available for any method.

A trait declares prototypes over `Self`, and an implementation binds them to one target type:

```r
trait Area { f64 area(const Self* this); };
impl Area for Circle { f64 area(const Circle* this) { ... } };

@generic<T: Area>
f64 total_area(const T* first, const T* second) { ... }
```

Dispatch is static. Instantiating `total_area` with `Circle` binds `area` to the definition of
the implementation, so the generated C17 contains direct calls only.

`Paintable : Area` requires an `Area` implementation and supplies `paint_cost` as a default
method. Both shapes reuse its checked body; an implementation may supply an exact-signature
override. The generic `painting_quote` function uses this inherited contract. The area and
pricing operations promise `@noalloc @nonblocking`, including the implementations selected
by the trait. An unused default body or implementation is still checked.

The batch estimate consumes a quote iterator and applies a captured discount. Both
factories return an opaque contract:

```r
opaque(core::Iterator & Item = f64) quote_range(f64 first, f64 step, usize count);
opaque(fn(f64) -> f64 & copy) discount(f64 fraction);
```

These are signature excerpts; opaque results require a definition. Callers use `auto`
and the advertised iterator or callable operations. `QuoteRange` is protected, and the
closure environment has no source-level name. Both remain concrete stack values with
ordinary cleanup; no boxing or runtime method table is needed. The fixed `Item` contract
lets the loop consume `f64` values while the range implementation remains private.

From the repository root, inspect the generated program with:

```sh
build-debug/r-front --module-map examples/methods/modules.map \
    --entry example.methods.main --emit=c17
ctest --test-dir build-debug -R r_frontend_codegen_method_example --output-on-failure
```
