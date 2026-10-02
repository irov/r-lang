module test.regression.finally_path_move_cleanup;

struct Resource {
    i32 marker;
};

protected i32 drop_count(i32 increment) {
    static i32 count = 0;
    unsafe {
        count += increment;
        return count;
    }
}

drop(Resource* self) {
    self->marker as void;
    i32 ignored = drop_count(1);
    ignored as void;
}

protected void probe(bool consume) {
    own Resource* retained = new Resource { .marker = 89 };
    try {
        if (consume == true) {
            own Resource* taken = move retained;
            drop taken;
            return;
        }
    } finally {
        i32 observed = drop_count(0);
        observed as void;
    }
}

i32 main() {
    i32 before = drop_count(0);
    probe(false);
    i32 after = drop_count(0);
    i32 selected = after == before + 1 ? 0 : 1;
    return selected;
}
