module audit.reinitialize_owner_before_nested_finally_control;

i32 main() {
    own i32* value = new i32(7);
    drop value;
    value = new i32(9);
    try {
        try {
            return 0;
        } finally {
            i32 marker = 0;
            marker as void;
        }
    } finally {
        i32 observed = *value;
        observed as void;
    }
}
