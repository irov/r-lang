module audit.return_nested_finally_reinitializes_owner;

i32 main() {
    own i32* value = new i32(7);
    drop value;
    try {
        try {
            return 0;
        } finally {
            value = new i32(9);
        }
    } finally {
        i32 observed = *value;
        observed as void;
    }
}
