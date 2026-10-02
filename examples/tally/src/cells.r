module example.tally.cells;

import std.string;

/* One column of a report line. */
trait Cell {
    std.string::string render(const Self* this) throws std.alloc::alloc_error;
};

impl Cell for i64 {
    std.string::string render(const i64* this) throws std.alloc::alloc_error {
        i64 value = *this;
        return f"{value}";
    }
};

impl Cell for usize {
    std.string::string render(const usize* this) throws std.alloc::alloc_error {
        usize value = *this;
        return f"{value}";
    }
};

impl Cell for bool {
    std.string::string render(const bool* this) throws std.alloc::alloc_error {
        if (*this == true) {
            return std.string::from_str("yes");
        }
        return std.string::from_str("no");
    }
};

/* A value with the label of its column, rendered as `label=value`. */
@generic<T: Cell & copy>
struct Named {
    str label;
    T value;
};

@generic<T: Cell & copy>
impl Cell for Named<T> {
    std.string::string render(const Named<T>* this) throws std.alloc::alloc_error {
        std.string::string value = this->value.render();
        str label = this->label;
        return f"{label}={value}";
    }
};

@generic<T: Cell & copy>
Named<T> named(str label, T value) {
    return Named<T> {.label = label, .value = value};
}

/* The columns separated by two spaces. Each call instantiates one function per number of
   columns; the static branch ends the recursion (Core R-TYPE-0053). */
@generic<Head: Cell, Tail...: Cell>
std.string::string line(Head head, Tail... tail) throws std.alloc::alloc_error {
    std.string::string text = head.render();
    move head as void;
    @if (len(Tail...) != 0usize) {
        std.string::string rest = line(...move tail);
        return f"{text}  {rest}";
    }
    move tail as void;
    return move text;
}

/* Count, sum, minimum and maximum of the readings, as one tuple (Core R-TYPE-0052). */
(usize, i64, i64, i64) summarize(const i64[] values) {
    i64 total = 0;
    i64 low = values[0usize];
    i64 high = values[0usize];
    for (usize index = 0usize; index < len(values); index += 1usize) {
        total += values[index];
        if (values[index] < low) {
            low = values[index];
        }
        if (values[index] > high) {
            high = values[index];
        }
    }
    return (len(values), total, low, high);
}
