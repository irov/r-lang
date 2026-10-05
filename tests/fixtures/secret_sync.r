module test.semantic.secret_sync;

/* R-SLIB-SECRET-0001: buffer is Send and Sync, so state that holds one may be shared through
   arc by tasks, and a scoped child may borrow it. */
struct Keys { std.secret::buffer key; };

async usize observe(arc Keys keys) {
    return std.secret::len(&keys->key);
}

@scoped
async usize borrowed(const std.secret::buffer* source) {
    return std.secret::len(source);
}

i32 main() {
    return 0;
}
