module test.regression.scoped_unborrowed_generic_local;

/* M25-1: a local whose dependent type its constraints prove unborrowed holds no view, so a
   @scoped method may borrow it in a task_scope of the function that returns it. */
@generic<T: send & sync & unborrowed>
struct holder { T value; u32 count; };

@generic<T: send & sync & unborrowed>
@scoped
protected async u32 holder<T>::look(const holder<T>* this) {
    return this->count;
}

@generic<T: send & sync & unborrowed>
async holder<T> make(T value) throws std.error::fault {
    holder<T> made = holder<T> {.value = move value, .count = 3u32};
    u32 seen = 0u32;
    task_scope(1) io { seen += await made.look(); }
    made.count += seen;
    return move made;
}

async i32 main() {
    holder<u64> result = await make(7u64);
    if (result.value != 7u64) { return 1; }
    if (result.count != 6u32) { return 2; }
    return 0;
}
