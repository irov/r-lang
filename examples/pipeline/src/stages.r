module example.pipeline.stages;

/* Checked errors of the individual stages. Each stage declares only its own failure. */
error OutOfRange { i64 value; i64 limit; };
error Overflow { i64 value; i64 factor; };

/* A stage that admits readings in [-limit, limit]. The returned callable keeps its limit by
   value, so the stage is an ordinary Copy value that the pipeline may call many times. */
opaque(fn(i64) -> i64 throws(OutOfRange) & copy) bounded(i64 limit) {
    fn i64 check(i64 value) move(limit) throws OutOfRange {
        if (value < -limit || value > limit) {
            throw OutOfRange {.value = value, .limit = limit};
        }
        return value;
    }
    return check;
}

/* A stage that multiplies each reading; an overflowing product is a checked error. */
opaque(fn(i64) -> i64 throws(Overflow) & copy) scaled(i64 factor) {
    fn i64 scale(i64 value) move(factor) throws Overflow {
        o<i64> product = core::checked_mul(value, factor);
        switch (product) {
        case variant o::some(result):
            return *result;
        case variant o::none:
            throw Overflow {.value = value, .factor = factor};
        }
    }
    return scale;
}

/* A stage without failures; its name is passed as a function item. */
i64 magnitude(i64 value) {
    if (value < 0) {
        return -value;
    }
    return value;
}

/* Sums two readings; an overflowing total is reported with factor 1. */
i64 add(i64 total, i64 value) throws Overflow {
    o<i64> sum = core::checked_add(total, value);
    switch (sum) {
    case variant o::some(result):
        return *result;
    case variant o::none:
        throw Overflow {.value = value, .factor = 1};
    }
}

/* Runs two stages over every reading in place. The adapter catches nothing, so it throws
   exactly the union of the stages' error sets (Core R-TYPE-0049): OutOfRange and Overflow for
   bounded then scaled, only OutOfRange for bounded then magnitude. */
@generic<E: errors & unborrowed, G: errors & unborrowed,
         F: fn(i64) -> i64 throws(E), H: fn(i64) -> i64 throws(G)>
void run(i64[] values, F first, H second) throws E, G {
    for (usize index = 0usize; index < len(values); index += 1usize) {
        i64 admitted = first(values[index]);
        values[index] = second(admitted);
    }
}

/* Combines all readings from an initial value with a callback that may fail. */
@generic<E: errors & unborrowed, F: fn(i64, i64) -> i64 throws(E)>
i64 fold(const i64[] values, i64 initial, F combine) throws E {
    i64 result = initial;
    for (usize index = 0usize; index < len(values); index += 1usize) {
        i64 next = combine(result, values[index]);
        result = next;
    }
    return result;
}
