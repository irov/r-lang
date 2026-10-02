module audit.aggregate_initializer_borrow_then_move_distinct_origins;

struct Coupled {
    const i32* borrowed;
    own i32* owner;
};

i32 main() {
    own i32* borrowed_owner = new i32(17);
    own i32* moved_owner = new i32(19);
    Coupled coupled = Coupled {
        .borrowed = &*borrowed_owner,
        .owner = move moved_owner,
    };
    own i32* replacement = new i32(23);
    coupled.owner = move replacement;
    i32 result = *(coupled.borrowed) - 17;
    drop coupled;
    drop borrowed_owner;
    return result;
}
