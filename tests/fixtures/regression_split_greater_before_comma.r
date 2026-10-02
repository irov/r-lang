module test.regression.split_greater_before_comma;

/* R-TYPE-0031: the second `>` of a `>>` token closes the enclosing type argument list; a
   ',' after it separates the items of the list around that type (throws effects, type
   arguments, callable parameter types, generic and call arguments). */

@generic<A, B>
struct Pair {
    A first;
    B second;
};

@generic<T>
usize width(const T* value) {
    return 1usize;
}

@generic<F: fn(const array<o<i32>>*, i32) -> usize>
usize apply(F callback, const array<o<i32>>* values) {
    return callback(values, 1);
}

usize measure(const array<o<i32>>* values, i32 extra) {
    return len(*values) + (extra as usize);
}

usize collect(array<o<str>>* texts, array<array<str>>* groups)
    throws std.array::push_error<o<str>>, std.array::push_error<array<str>> {
    std.array::push(texts, o::none);
    std.array::push(groups, std.array::create::<str>());
    return len(*texts) + len(*groups);
}

usize pairs(const Pair<o<o<i32>>, i32>* left, const Pair<i32, o<o<i32>>>* right) {
    return width::<o<o<i32>>>(&left->first) + width::<o<o<i32>>>(&right->second);
}

usize sum(usize a, usize b) {
    return a + b;
}

i32 main() {
    array<o<str>> texts = std.array::create::<o<str>>();
    array<array<str>> groups = std.array::create::<array<str>>();
    Pair<o<o<i32>>, i32> left = {.first = o::none, .second = 1};
    Pair<i32, o<o<i32>>> right = {.first = 2, .second = o::none};
    try {
        usize collected = collect(&texts, &groups);
        array<o<i32>> values = std.array::create::<o<i32>>();
        usize total = sum(sum(pairs(&left, &right), collected), apply(measure, &values));
        if (total != 5usize) {
            return 3;
        }
    } catch (std.array::push_error<o<str>> error) {
        return 1;
    } catch (std.array::push_error<array<str>> error) {
        return 2;
    }
    return 0;
}
