module audit.aggregate_initializer_unreachable_move_same_origin;

struct Coupled {
    const i32* borrowed;
    own i32* owner;
};

i32 main() {
    own i32* owner = new i32(17);
    own i32* other = new i32(23);
    Coupled coupled = Coupled {
        .borrowed = &*owner,
        .owner = false ? move owner : move other,
    };
    i32 result = *(coupled.borrowed) - 17;
    drop coupled;
    drop owner;
    return result;
}
