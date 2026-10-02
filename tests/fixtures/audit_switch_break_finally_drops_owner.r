module audit.switch_break_finally_drops_owner;

i32 main() {
    own i32* value = new i32(7);
    switch (0) {
        case 0:
            try {
                break;
            } finally {
                drop value;
            }
            break;
        default:
            break;
    }
    return *value;
}
