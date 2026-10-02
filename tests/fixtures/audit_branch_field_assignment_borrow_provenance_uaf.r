module audit.branch_field_assignment_borrow_provenance_uaf;

struct Holder {
    own i32* left;
    own i32* right;
    const i32* alias;
};

i32 run(bool choose_left) {
    i32 initial = 0;
    own i32* left = new i32(7);
    own i32* right = new i32(9);
    Holder holder = Holder {
        .left = move left,
        .right = move right,
        .alias = &initial,
    };
    if (choose_left == true) {
        holder.alias = &*holder.left;
    } else {
        holder.alias = &*holder.right;
    }
    holder.left = new i32(11);
    if (choose_left == true) {
        return *holder.alias;
    }
    return 0;
}

i32 main() {
    i32 result = run(true);
    return result;
}
