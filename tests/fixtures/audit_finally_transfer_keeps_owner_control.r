module audit.finally_transfer_keeps_owner_control;

error Failure {
    i32 code;
};

protected i32 break_control() {
    own i32* value = new i32(7);
    while (true) {
        try {
            break;
        } finally {
            *value += 1;
        }
    }
    return *value - 8;
}

protected i32 continue_control() {
    own i32* value = new i32(7);
    bool repeat = true;
    while (repeat == true) {
        try {
            repeat = false;
            continue;
        } finally {
            *value += 1;
        }
    }
    return *value - 8;
}

protected i32 switch_control() {
    own i32* value = new i32(7);
    switch (0) {
        case 0:
            try {
                break;
            } finally {
                *value += 1;
            }
            break;
        default:
            break;
    }
    return *value - 8;
}

protected i32 throw_control() {
    own i32* value = new i32(7);
    try {
        try {
            throw Failure {
                .code = 1,
            };
        } finally {
            *value += 1;
        }
    } catch (Failure failure) {
        failure.code as void;
    }
    return *value - 8;
}

i32 main() {
    i32 first = break_control();
    i32 second = continue_control();
    i32 third = switch_control();
    i32 fourth = throw_control();
    return first + second + third + fourth;
}
