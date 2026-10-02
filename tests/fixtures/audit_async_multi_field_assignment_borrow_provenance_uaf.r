module audit.async_multi_field_assignment_borrow_provenance_uaf;

struct Holder {
    own i32* left;
    own i32* right;
    const i32* left_alias;
    const i32* right_alias;
};

async i32 main() {
    i32 initial_left = 0;
    i32 initial_right = 0;
    own i32* left = new i32(7);
    own i32* right = new i32(9);
    Holder holder = Holder {
        .left = move left,
        .right = move right,
        .left_alias = &initial_left,
        .right_alias = &initial_right,
    };
    holder.left_alias = &*holder.left;
    holder.right_alias = &*holder.right;
    holder.left = new i32(11);
    return *holder.left_alias;
}
