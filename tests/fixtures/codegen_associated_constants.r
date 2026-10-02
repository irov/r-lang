module test.codegen.associated_constants;

/* R-TYPE-0050: associated constants of traits, with defaults, over generic parameters, generic
   implementations and closed types, in bounds, conditions and constant arguments. */

trait Shape {
    const usize SIDES;
    const usize DOUBLE = Self::SIDES * 2usize;
    const u32 MAGIC = 7u32;
    usize corners(const Self* this) { return Self::SIDES + Self::DOUBLE; }
};

trait Report : Shape {
    const bool VERBOSE = false;
};

@generic<U>
trait Codec {
    const usize WIDTH = sizeof(U);
    const u8 TAG;
};

struct Tri { u8 id; };
struct Quad { u32 x; u32 y; };

impl Shape for Tri {
    const usize SIDES = 3usize;
    usize corners(const Tri* this) { return Self::SIDES + (this->id as usize); }
};

impl Shape for Quad {
    const usize SIDES = sizeof(Self) / 2usize;
    const u32 MAGIC = 11u32;
};

impl Report for Quad {
    const bool VERBOSE = true;
};

impl Codec<u16> for Quad {
    const u8 TAG = 2u8;
};

impl Codec<u64> for Quad {
    const u8 TAG = 3u8;
};

@generic<const usize N>
struct Buffer { u8[N] bytes; };

@generic<const usize N>
impl Shape for Buffer<N> {
    const usize SIDES = N;
};

@generic<const u32 M>
u32 mask() { return M; }

/* A dependent constant sizes arrays and supplies constant arguments. */
@generic<T: Shape>
usize total(const T* value) {
    Buffer<T::SIDES> buffer = {};
    u8[T::DOUBLE] twice = {};
    value as void;
    return len(buffer.bytes) + len(twice) + (mask::<T::MAGIC>() as usize);
}

@generic<T: Report>
usize reported() {
    @if (T::VERBOSE == true) {
        return T::SIDES * 10usize;
    } @else {
        return T::SIDES;
    }
}

@generic<T: Codec<u64>>
usize wide() { return T::WIDTH + (T::TAG as usize); }

@generic<T: Shape>
struct Frame { u8[T::SIDES] payload; };

/* Module scope reads the values of implementations. */
struct Wire { Frame<Tri> tri; Frame<Quad> quad; u8[Tri::SIDES] row; };
const usize WIRE_SIDES = Tri::SIDES + Quad::SIDES;

@if (Quad::SIDES == 4usize) {
    const i32 LAYOUT = 1;
} @else {
    const i32 LAYOUT = missing_layout;
}

i32 main() {
    Tri tri = {.id = 1u8};
    Quad quad = {.x = 1u32, .y = 2u32};
    Buffer<5usize> five = {};
    Wire wire = {};
    bool direct = Tri::SIDES == 3usize && Tri::DOUBLE == 6usize && Quad::MAGIC == 11u32 &&
                  Tri::MAGIC == 7u32 && Buffer<5usize>::SIDES == 5usize &&
                  Buffer<5usize>::DOUBLE == 10usize;
    bool methods =
        tri.corners() == 4usize && quad.corners() == 12usize && five.corners() == 15usize;
    bool generic = total(&tri) == 3usize + 6usize + 7usize &&
                   total(&quad) == 4usize + 8usize + 11usize &&
                   total(&five) == 5usize + 10usize + 7usize && reported::<Quad>() == 40usize &&
                   wide::<Quad>() == 11usize;
    bool module_ok = len(wire.tri.payload) == 3usize && len(wire.quad.payload) == 4usize &&
                  len(wire.row) == 3usize && WIRE_SIDES == 7usize && LAYOUT == 1;
    if (direct == false || methods == false || generic == false || module_ok == false) {
        return 1;
    }
    return 0;
}
