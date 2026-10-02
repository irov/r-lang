module audit.loop_continue_finally_drops_owner;

i32 main() {
    own i32* value = new i32(7);
    bool repeat = true;
    while (repeat == true) {
        try {
            repeat = false;
            continue;
        } finally {
            drop value;
        }
    }
    return *value;
}
