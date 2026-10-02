module test.codegen.async_enum;

enum signed_state {
    idle,
    ready,
};

enum unsigned_state : u32 {
    cold,
    warm,
};

async i32 main() {
    signed_state signed_value = signed_state::ready;
    unsigned_state unsigned_value = unsigned_state::warm;

    if (signed_value == signed_state::ready) {
        if (unsigned_value == unsigned_state::warm) {
            return 0;
        }
        return 2;
    }
    return 1;
}
