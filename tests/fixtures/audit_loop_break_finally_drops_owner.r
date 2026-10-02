module audit.loop_break_finally_drops_owner;

i32 main() {
    own i32* value = new i32(7);
    while (true) {
        try {
            break;
        } finally {
            drop value;
        }
    }
    return *value;
}
