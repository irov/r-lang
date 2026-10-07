module test.codegen.string_slice_params;

/* R-BORROW-0018, R-BORROW-0008: a `const str[]` parameter reads, returns and subslices strings
   whose origins stay those of the caller's elements; functions, methods, generic instances,
   callables and scoped async frames take it like any other input view. */
usize count_long(const str[] words, usize minimum) {
    usize total = 0usize;
    for (usize i = 0usize; i < len(words); i += 1usize) {
        if (len(words[i]) >= minimum) {
            total += 1usize;
        }
    }
    return total;
}

str longest(const str[] words) {
    str best = words[0];
    for (usize i = 1usize; i < len(words); i += 1usize) {
        if (len(words[i]) > len(best)) {
            best = words[i];
        }
    }
    return best;
}

const str[] rest(const str[] words) { return words[1usize..len(words)]; }

struct Filter { usize minimum; };
usize Filter::matches(const Filter* this, const str[] words) {
    return count_long(words, this->minimum);
}

@generic<T> usize count(const T[] items) { return len(items); }

@generic<F: fn(const str[]) -> usize>
usize apply(F f, const str[] words) { return f(words); }

usize measure(const str[] words) { return len(words) * 10usize; }

async void pause() {}

@scoped
async usize scoped_count(const str[] words) throws std.async::start_error {
    await pause();
    return count_long(words, 2usize);
}

async i32 main() {
    str[3] local = {"a", "bbb", "cc"};
    const str[] view = local[0usize..3usize];
    if (count_long(view, 2usize) != 2usize) { return 1; }
    str best = longest(view);
    if (len(best) != 3usize) { return 2; }
    const str[] tail = rest(view);
    if (len(tail) != 2usize || len(tail[1]) != 2usize) { return 3; }
    Filter filter = {.minimum = 2usize};
    if (filter.matches(view) != 2usize) { return 4; }
    if (count(view) != 3usize || apply(measure, view) != 30usize) { return 5; }
    fn usize shortest(const str[] words) {
        usize result = len(words[0]);
        for (usize i = 1usize; i < len(words); i += 1usize) {
            if (len(words[i]) < result) {
                result = len(words[i]);
            }
        }
        return result;
    }
    if (apply(shortest, view) != 1usize) { return 6; }
    // Elements borrowed from an owned string keep that string as their origin.
    std.string::string owned = std.string::from_str("owned text");
    str[2] mixed = {owned, "x"};
    str owned_best = longest(mixed[0usize..2usize]);
    if (len(owned_best) != 10usize) { return 7; }
    try {
        task_scope(1) group {
            usize scoped = await scoped_count(view);
            if (scoped != 2usize) { return 8; }
        }
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
    return 0;
}
