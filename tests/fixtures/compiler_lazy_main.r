module compiler.lazy.main;

import compiler.lazy.dependency::{Item};

i32 main() {
    Item item = {
        .value = 7,
    };
    return item.value;
}
