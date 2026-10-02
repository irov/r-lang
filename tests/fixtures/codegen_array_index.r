module test.codegen.array_index;

/* R-EXPR-0021: `values[i]` designates the live element in the buffer an array<T> owns, with a
   bounds check; reads, writes, element borrows, nested arrays, elements holding views and
   owned elements that are replaced and dropped. */

struct Named { str name; i32 id; };

i32 sum(const array<i32>* values) {
    i32 total = 0;
    for (usize index = 0usize; index < len(*values); index += 1usize) {
        total += (*values)[index];
    }
    return total;
}

void double_all(array<i32>* values) {
    for (usize index = 0usize; index < len(*values); index += 1usize) {
        (*values)[index] *= 2;
    }
}

async i32 async_pick(i32 seed) {
    array<i32> values = std.array::create::<i32>();
    try {
        std.array::push(&values, seed);
        std.array::push(&values, seed + 1);
    } catch (std.array::push_error<i32> failure) {
        move failure as void;
        return -1;
    }
    values[1] += 100;
    return values[0] + values[1];
}

i32 main() {
    try {
        array<i32> values = std.array::create::<i32>();
        std.array::push(&values, 1);
        std.array::push(&values, 2);
        std.array::push(&values, 3);
        double_all(&values);
        if (sum(&values) != 12) { return 2; }
        i32* last = &values[2];
        *last = 100;
        if (values[2] != 100) { return 6; }
        array<array<i32>> grid = std.array::create::<array<i32>>();
        array<i32> row = std.array::create::<i32>();
        std.array::push(&row, 7);
        std.array::push(&row, 8);
        std.array::push(&grid, move row);
        grid[0][1] += grid[0][0];
        if (grid[0][1] != 15) { return 3; }
        array<Named> names = std.array::create::<Named>();
        std.array::push(&names, Named {.name = "first", .id = 1});
        std.array::push(&names, Named {.name = "second", .id = 2});
        names[0].name = names[1].name;
        names[1].id = names[0].id + 10;
        if ((len(names[0].name) != 6usize) || (names[1].id != 11)) { return 4; }
        array<std.string::string> owned = std.array::create::<std.string::string>();
        std.array::push(&owned, std.string::from_str("old"));
        owned[0] = std.string::from_str("replacement");
        if (std.string::len(&owned[0]) != 11usize) { return 5; }
    } catch (std.array::push_error<i32> failure) {
        move failure as void;
        return 1;
    } catch (std.array::push_error<array<i32>> failure) {
        move failure as void;
        return 1;
    } catch (std.array::push_error<Named> failure) {
        move failure as void;
        return 1;
    } catch (std.array::push_error<std.string::string> failure) {
        move failure as void;
        return 1;
    }
    return 0;
}
