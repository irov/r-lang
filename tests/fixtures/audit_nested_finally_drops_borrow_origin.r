module audit.nested_finally_drops_borrow_origin;

i32 main() {
    own i32* value = new i32(7);
    const i32* alias = &*value;
    try {
        try {
            return 1;
        } finally {
            drop value;
        }
    } finally {
        i32 observed = *alias;
        observed as void;
    }
}
