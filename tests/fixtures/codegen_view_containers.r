module test.codegen.view_containers;

/* R-BORROW-0018, R-LIB-0019..R-LIB-0022, R-EXPR-0030: containers of shared borrows and shared
   slices, collection literals of slices and pushing a slice. The container carries the origins
   of the views it holds. An insertion error carries the staged view (R-BORROW-0025), so it is
   handled while the viewed locals exist. A Copy payload holding a view moves out of a variant,
   and a Copy insertion error is switched on. */

struct Point { i32 x; i32 y; };
i32 sum_points(const list<const Point*>* points) {
    i32 total = 0;
    for (const (const Point*)* item in points) { total += (**item).x; }
    return total;
}
usize total_parts(const array<const i32[]>* parts) {
    const (const i32[])[] view = std.array::as_slice(parts);
    usize total = 0usize;
    for (usize index = 0usize; index < len(view); index += 1usize) { total += len(view[index]); }
    return total;
}
i32 probe() {
    Point a = {.x = 1, .y = 2};
    Point b = {.x = 10, .y = 20};
    i32[4] values = {1, 2, 3, 4};
    try {
        list<const Point*> points = std.list::create::<const Point*>();
        std.list::push_back(&points, &a) as void;
        std.list::push_back(&points, &b) as void;
        const i32[] head = values[0usize..2usize];
        array<const i32[]> parts = std.array::create::<const i32[]>();
        std.array::push(&parts, head);
        std.array::push(&parts, values[1usize..4usize]);
        array<const i32[]> literal = [values[0usize..1usize], head];
        dict<i32, const Point*> index = std.dict::create::<i32, const Point*>();
        std.dict::insert(&index, 1, &a) as void;
        return sum_points(&points) + (total_parts(&parts) as i32) + (len(literal) as i32) +
               (len(index) as i32);
    } catch (std.list::push_error<const Point*> e) {
        switch (move e) {
        case variant std.list::push_error::allocation_failed(move payload):
            return -(*payload.value).x;
        }
    } catch (std.array::push_error<const i32[]> e) {
        e as void;
        return -10;
    } catch (std.dict::insert_error<i32, const Point*> e) {
        e as void;
        return -100;
    }
}
struct Pair { const i32* value; i32 bonus; };
enum Holder { held(Pair), empty };
i32 unpack(Holder holder) {
    switch (holder) {
    case variant Holder::held(move pair): return *pair.value + pair.bonus;
    case variant Holder::empty: return 0;
    }
}
i32 count_values() {
    list<i32> values = std.list::create::<i32>();
    try {
        std.list::push_back(&values, 3) as void;
        return len(values) as i32;
    } catch (std.list::push_error<i32> failure) {
        switch (failure) {
        case variant std.list::push_error::allocation_failed: return -1;
        }
    }
}
i32 main() {
    if (probe() != 11 + 5 + 2 + 1) { return 1; }
    i32 value = 5;
    if (unpack(Holder::held(Pair {.value = &value, .bonus = 1})) != 6) { return 2; }
    if (count_values() != 1) { return 3; }
    return 0;
}
