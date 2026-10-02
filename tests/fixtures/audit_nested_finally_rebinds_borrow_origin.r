module audit.nested_finally_rebinds_borrow_origin;

i32 main() {
    own i32* first = new i32(7);
    own i32* second = new i32(9);
    const i32* alias = &*second;
    try {
        try {
            return 0;
        } finally {
            alias = &*first;
        }
    } finally {
        drop first;
        i32 observed = *alias;
        observed as void;
    }
}
