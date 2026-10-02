module audit.aggregate_checked_initializer_cleanup;

protected i32 drop_trace(i32 marker) {
    static i32 trace = 0;
    unsafe {
        if (marker != 0) { trace = trace * 10 + marker; }
        return trace;
    }
}

struct Resource { i32 marker; };

drop(Resource* self) {
    i32 ignored = drop_trace(self->marker);
    ignored as void;
}

struct Inner {
    Resource second;
    i32 checked;
};

struct Outer {
    Resource first;
    Inner inner;
};

error Failure {};

protected i32 fail() throws Failure {
    throw Failure {};
}

protected i32 exercise() {
    i32 before = drop_trace(0);
    before as void;
    try {
        Outer value = {
            .first = Resource {.marker = 1},
            .inner = Inner {
                .second = Resource {.marker = 2},
                .checked = fail(),
            },
        };
        drop value;
        return 1;
    } catch (Failure error) {
        error as void;
    }
    i32 after = drop_trace(0);
    if (after != before * 100 + 21) { return 2; }
    return 0;
}

i32 main() {
    i32 result = exercise();
    return result;
}
