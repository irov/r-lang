module test.regression.condition_short_circuit_inconsistent_move;

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
}

protected bool consume(own Resource* value) {
    drop value;
    return true;
}

protected void probe(bool selected) {
    own Resource* retained = new Resource { .marker = 73 };
    if (selected == true && consume(move retained) == true) {
        return;
    }
}

i32 main() {
    i32 before = drop_count(0);
    probe(false);
    i32 after = drop_count(0);
    i32 chosen = after == before + 1 ? 0 : 1;
    return chosen;
}
