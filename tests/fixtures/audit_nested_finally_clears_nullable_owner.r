module audit.nested_finally_clears_nullable_owner;

i32 main() {
    own i32*? value = new i32(7);
    try {
        try {
            return 0;
        } finally {
            value = null;
        }
    } finally {
        i32 observed = *value;
        observed as void;
    }
}
