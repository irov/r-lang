module test.regression.enum_switch_disjoint_move;

enum Choice {
    Take,
    Keep,
};

protected i32 choose(Choice selected) {
    own i32* retained = new i32(79);
    switch (selected) {
    case Choice::Take:
        own i32* consumed = move retained;
        drop consumed;
        return 7;
    case Choice::Keep:
        if (*retained != 79) {
            return 1;
        }
        return 0;
    }
}

i32 main() {
    i32 result = choose(Choice::Keep);
    return result;
}
