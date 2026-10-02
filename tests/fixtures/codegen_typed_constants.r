module test.codegen.typed_constants;

/* R-TYPE-0047: constant generic parameters of integer types and bool. */

@generic<const bool CHECKED>
struct Guard { u32 value; };

/* A bool parameter decides a static condition for each instance. */
@generic<const bool CHECKED>
u32 read(const Guard<CHECKED>* guard) {
    @if (CHECKED == true) {
        if (guard->value > 100u32) {
            return 100u32;
        }
    }
    return guard->value;
}

@generic<const u32 MASK>
u32 apply(u32 value) {
    return value & MASK;
}

@generic<const i32 OFFSET>
i32 shift(i32 value) {
    return value + OFFSET;
}

/* A typed parameter sizes an array through a computed formula. */
@generic<const u8 WIDTH>
struct Row { u8[WIDTH as usize] cells; };

@generic<const u64 N>
u64 twice() {
    return N * 2u64;
}

@generic<const i8 N, const bool NEGATE>
i32 signed_value() {
    @if (NEGATE == true) {
        return 0 - (N as i32);
    } @else {
        return N as i32;
    }
}

i32 main() {
    Guard<true> checked = {.value = 500u32};
    Guard<false> plain = {.value = 500u32};
    Row<3u8> row = {};
    Row<3> same = row;
    bool guards = read(&checked) == 100u32 && read(&plain) == 500u32;
    bool masks = apply::<0x0Fu32>(0xABu32) == 0x0Bu32 && shift::<-5>(10) == 5;
    bool rows = len(same.cells) == 3usize && twice::<21u64>() == 42u64;
    bool signs = signed_value::<-3, true>() == 3 && signed_value::<-3, false>() == -3;
    if (guards == false || masks == false || rows == false || signs == false) {
        return 1;
    }
    return 0;
}
