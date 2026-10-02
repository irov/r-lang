module test.aggregate.consumer;

import test.aggregate.api::{point, state};

protected point bump(point input) {
    input.x = input.x + 3;
    return input;
}

i32 main() {
    point value = {
        .y = 2,
        .x = 1,
    };
    state current = state::ready;
    value = bump(value);
    if (current == state::ready) {
        return value.x;
    }
    return 1;
}
