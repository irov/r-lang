module test.codegen.generic_callable_results;

/* R-TYPE-0044, R-TYPE-0035: inside a generic body, the borrow that a callable constraint
   returns is dereferenced, projected, indexed and written through directly, as the result of an
   ordinary call is. */

struct Cell {
    i32 value;
    i32 tag;
};

const i32* same(const i32* item) {
    return item;
}

@generic<F: fn(const i32*) -> const i32*>
i32 peek(F f, const i32* item) {
    return *f(item) + 1;
}

@generic<F: fn(const i32*) -> const i32*>
i32 peek_through(const F* f, const i32* item) {
    i32 twice = *f(item) * 2;
    return twice;
}

@generic<F: fn(const Cell*) -> const Cell*>
i32 tag_of(const F* f, const Cell* cell) {
    return f(cell)->tag + (*f(cell)).value;
}

@generic<F: fn(const i32[]) -> const i32[]>
i32 second(const F* f, const i32[] items) {
    return f(items)[1];
}

@generic<F: fn(i32*) -> i32*>
void bump(const F* f, i32* item) {
    *f(item) += 5;
}

@generic<F: fn(Cell*) -> Cell*>
void retag(const F* f, Cell* cell) {
    f(cell)->tag = 9;
}

i32 main() {
    i32 value = 4;
    if (peek(same, &value) != 5) {
        return 1;
    }
    fn const i32* pass(const i32* item) { return item; }
    if (peek_through(&pass, &value) != 8) {
        return 2;
    }
    Cell cell = Cell {.value = 3, .tag = 7};
    fn const Cell* keep(const Cell* item) { return item; }
    if (tag_of(&keep, &cell) != 10) {
        return 3;
    }
    (i32)[3] items = {1, 2, 3};
    fn const i32[] tail(const i32[] all) { return all[1usize..]; }
    if (second(&tail, items[..]) != 3) {
        return 4;
    }
    fn i32* through(i32* item) { return item; }
    bump(&through, &value);
    if (value != 9) {
        return 5;
    }
    fn Cell* edit(Cell* item) { return item; }
    retag(&edit, &cell);
    if (cell.tag != 9) {
        return 6;
    }
    return 0;
}
