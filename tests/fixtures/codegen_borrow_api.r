module codegen.borrow_api;

struct Cell {
    i32 value;
    i32[2] items;
};

void increment(i32* value) {
    *value += 1;
}

i32 read(const Cell* cell) {
    return cell->value;
}

i32 read_item(const Cell* cell, usize index) {
    return cell->items[index];
}

i32 sum(const Cell* left, const Cell* right) {
    return left->value + right->value;
}

const i32* identity(const i32* value) {
    return value;
}
