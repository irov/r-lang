module test.audit.unary_postfix_precedence;

struct Holder {
    const i32* value;
    i32 number;
    bool flag;
};

i32 main() {
    i32 local = 41;
    Holder holder = Holder { .value = &local, .number = 7, .flag = true };
    if (-holder.number != -7) {
        return 1;
    }
    if (~holder.number != -8) {
        return 2;
    }
    bool inverted = !holder.flag;
    if (inverted != false) {
        return 3;
    }
    return *holder.value - 41;
}
