# Static reflection

Reflection in R is resolved at translation time: every form is a compiler-recognized
`core::` item, nothing is looked up through run-time metadata, and the generated C17 contains
only literals, `switch` statements and static tables. The example builds a small logging model
(`src/model.r`) and inspects it from `src/main.r`.

Constants over a type are folded by the compiler and are call-free, so they may be passed as
arguments directly:

```r
enum Level : u8 { trace = 0, debug = 10, info = 20, warn = 30, error = 40, };

usize count = core::enum_count::<Level>();      // 5
Level lowest = core::enum_min::<Level>();       // Level::trace (least discriminant)
Level[5] all = core::enum_variants::<Level>();  // every variant in declaration order
usize fields = core::field_count::<Settings>(); // 3
ok = same(core::field_name::<Settings>(0), "threshold");
ok = same(core::type_name::<array<Level>>(), "array<example.reflection.model::Level>");
```

Selections over a value are ordinary calls that the emitter lowers to a `switch` over the
enumeration value, the index or the active tag, or to a search of a static table of the
declared names:

```r
constexpr str name = core::enum_name(level);          // "warn"
usize ordinal = core::enum_ordinal(level);            // 3
o<Level> next = core::enum_at::<Level>(ordinal + 1);    // o::some(Level::error)
o<Level> parsed = core::enum_from_name::<Level>(text);  // o::none for an unknown name
constexpr str active = core::variant_name(&command);  // "say" for Command::say { ... }
```

Inside a generic body a form over the parameter is folded when the parameter is substituted,
so each instantiation sees its own type:

```r
@generic<T: copy>
constexpr str type_of(T value) {
    constexpr str name = core::type_name::<T>();
    value as void;
    return name;
}

constexpr str spelled = type_of(warn);   // "example.reflection.model::Level"
constexpr str scalar = type_of(42u32);   // "u32"
```

`core::target_name()` and `core::profile_name()` name the target triple and the selected
library profile of the translation. The forms are specified by Core R-REFL-0001..0004 and
listed by Library R-LIB-0024; they are available in every profile, including `freestanding`.

From the repository root, inspect the generated program with:

```sh
build-debug/r-front --module-map examples/reflection/modules.map \
    --entry example.reflection.main --emit=c17
ctest --test-dir build-debug -R r_frontend_codegen_reflection_example --output-on-failure
```

In the output, every selection over `Level` is one static function such as
`r_reflection_enum_name_a…` (a `switch` assigning program strings) or
`r_reflection_enum_from_name_a…` (a `static const` table of the variant names searched by the
generated `r_reflection_find_name` helper), and every call site is one assignment call of it.
