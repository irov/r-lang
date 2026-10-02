module test.codegen.associated_constant_instances;

/* R-TYPE-0050: each closed instance of a generic implementation substitutes its own constant,
   however many instances one function body names. */
trait Encoded {
    const usize SIZE;
    const usize PADDED = Self::SIZE + 4usize;
};
@generic<const usize N>
struct Blob { u8[N] bytes; };
@generic<const usize N>
impl Encoded for Blob<N> { const usize SIZE = N; };
@generic<T: Encoded>
usize describe() { return T::SIZE; }
@generic<const usize N>
struct Fixed { u8[N] data; };

i32 main() {
    u8[Blob<4usize>::SIZE] first = {};
    u8[Blob<5usize>::SIZE] second = {};
    Fixed<Blob<6usize>::PADDED> fixed = {};
    if (len(first) != 4usize) { return 1; }
    if (len(second) != 5usize) { return 2; }
    if (len(fixed.data) != 10usize) { return 3; }
    if (describe::<Blob<7usize>>() != 7usize) { return 4; }
    if (describe::<Blob<4usize>>() != 4usize) { return 5; }
    return 0;
}
