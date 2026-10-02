module audit.switch_nested_break_targets;

protected i32 break_exits_switch_not_loop() {
    i32 iterations = 0;
    while (iterations < 2) {
        switch (iterations) {
            case 0:
                if (iterations == 0) {
                    break;
                } else {
                    return 10;
                }
                break;
            default:
                break;
        }
        iterations += 1;
    }
    return iterations;
}

protected i32 break_exits_inner_loop() {
    i32 iterations = 0;
    switch (0) {
        case 0:
            while (true) {
                iterations += 1;
                if (iterations == 1) {
                    break;
                }
            }
            break;
        default:
            return 10;
    }
    return iterations;
}

protected i32 break_runs_finally(bool leave) {
    i32 trace = 0;
    switch (0) {
        case 0:
            try {
                if (leave == true) {
                    break;
                } else {
                    trace = 2;
                }
            } finally {
                trace += 3;
            }
            trace += 10;
            break;
        default:
            return 100;
    }
    return trace;
}

i32 main() {
    if (break_exits_switch_not_loop() != 2) {
        return 1;
    }
    if (break_exits_inner_loop() != 1) {
        return 2;
    }
    if (break_runs_finally(true) != 3) {
        return 3;
    }
    if (break_runs_finally(false) != 15) {
        return 4;
    }
    return 0;
}
