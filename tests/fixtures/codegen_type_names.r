module test.codegen.type_names;

/* R-REFL-0003: constant arguments are spelled as their literals and interfaces by their
   canonical contracts, so distinct instances have distinct names. */
@generic<T, const usize N>
struct Buffer { T[N] data; };
@generic<const bool B, const i32 K>
struct Flags { u8 v; };
trait Store { u32 get(const Self* this); };

i32 main() {
    constexpr str four = core::type_name::<Buffer<u16, 4usize>>();
    constexpr str sixteen = core::type_name::<Buffer<u16, 16usize>>();
    constexpr str flags = core::type_name::<Flags<true, -3>>();
    constexpr str view = core::type_name::<const dyn(Store)*>();
    if (len(four) != 44usize) { return 1; }
    if (len(sixteen) != 45usize) { return 2; }
    if (len(flags) != 43usize) { return 3; }
    if (len(view) != 42usize) { return 4; }
    return 0;
}
