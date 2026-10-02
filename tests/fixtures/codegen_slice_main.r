module codegen.slice_main;

import codegen.slice_api::{WIDTH, middle};

i32 main() {
    u8[WIDTH] bytes = {1, 2, 3};
    const u8[] view = &bytes;
    u8 value = middle(view);
    if (value == 2) {
        return 0;
    } else {
        return 1;
    }
}
