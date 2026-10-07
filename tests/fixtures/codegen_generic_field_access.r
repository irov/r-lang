module test.codegen.generic_field_access;

/* R-STMT-0023, R-REFL-0006, R-TYPE-0043 (L44): a generic function walks the fields of any struct
   whose fields implement a trait, `@generic<T: fields(Weigh)>`; each instantiation unrolls the
   loop with the types of its own fields. */

trait Weigh {
    usize weight(const Self* this);
};

impl Weigh for u32 { usize weight(const u32* this) { return 4usize; } };
impl Weigh for u64 { usize weight(const u64* this) { return 8usize; } };
impl Weigh for bool { usize weight(const bool* this) { return 1usize; } };

trait Grow {
    void grow(Self* this);
};

impl Grow for u32 { void grow(u32* this) { *this += 1u32; } };
impl Grow for bool { void grow(bool* this) { *this = true; } };

struct Pair {
    u32 left;
    bool flag;
};

struct Triple {
    u64 big;
    u32 small;
    bool flag;
};

@generic<T: fields(Weigh)>
usize total(const T* value) {
    usize sum = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        usize weight = core::field(value, index)->weight();
        sum += weight;
    }
    return sum;
}

@generic<T: fields(Grow)>
void grow_all(T* value) {
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        core::field_mut(value, index)->grow();
    }
}

/* The loop constant also names the fields and decides constant conditions in a generic body. */
@generic<T: fields(Weigh)>
usize named(const T* value) {
    usize letters = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        str name = core::field_name::<T>(index);
        @if (index == 0usize) {
            letters += len(name) * 100usize;
        } @else {
            letters += len(name);
        }
        value as void;
    }
    return letters;
}

/* Defect L44-13: a branch that the constant of a repetition does not take copies nothing, so an
   array bound that would be zero there is no error. Defect L44-14: a bound computed from the
   parameters, here one less than the field count. */
@generic<T: fields(Weigh)>
usize branches(const T* value) {
    usize sum = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        @if (index > 0usize) {
            u8[index] buffer = {};
            sum += len(buffer) * 100usize;
        }
        sum += core::field(value, index)->weight();
    }
    for (constexpr usize index in 0usize..core::field_count::<T>() - 1usize) {
        sum += 1000usize;
    }
    return sum;
}

/* Defect L44-9: nested loops whose inner bounds name the outer constant intern a type per
   repetition while the outer loop is cloned. */
@generic<const usize N>
usize nested() {
    usize sum = 0usize;
    for (constexpr usize outer in 0usize..N) {
        for (constexpr usize inner in outer..outer + 1usize) {
            sum += inner;
        }
    }
    return sum;
}

i32 main() {
    Pair pair = {.left = 1u32, .flag = false};
    Triple triple = {.big = 2u64, .small = 3u32, .flag = false};
    if (total(&pair) != 5usize) { return 1; }
    if (total(&triple) != 13usize) { return 2; }
    grow_all(&pair);
    if (pair.left != 2u32 || pair.flag == false) { return 3; }
    if (named(&pair) != 404usize) { return 4; }
    if (named(&triple) != 309usize) { return 5; }
    if (branches(&triple) != 2313usize) { return 6; }
    if (nested::<300usize>() != 44850usize) { return 7; }
    return 0;
}
