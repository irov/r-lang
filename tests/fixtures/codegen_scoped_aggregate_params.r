module test.codegen.scoped_aggregate_params;

/* R-BORROW-0018, R-BORROW-0024, R-STMT-0017: a @scoped async function takes an aggregate that
   holds a borrow, by value or through a borrow. The enclosing task group keeps the storage the
   child borrows alive while the parent is suspended. */

struct Window { const i32[] values; usize start; };
struct Named { str name; i32 id; };
async void pause() {}
@scoped async usize sum_window(Window window) throws std.async::start_error {
    await pause();
    usize total = 0usize;
    for (usize index = window.start; index < len(window.values); index += 1usize) {
        total += window.values[index] as usize;
    }
    return total;
}
@scoped async usize name_length(const Named* named) throws std.async::start_error {
    await pause();
    return len(named->name);
}
async i32 main() {
    i32[4] data = {1, 2, 3, 4};
    Window window = {.values = data[0usize..4usize], .start = 1usize};
    Named named = {.name = "abc", .id = 1};
    try {
        task_scope(2) group {
            task<usize throws std.async::start_error> a = sum_window(window);
            task<usize throws std.async::start_error> b = name_length(&named);
            usize first = await move a;
            usize second = await move b;
            if (first * 10usize + second != 93usize) { return 1; }
        }
    } catch (std.async::start_error e) { e as void; return 2; }
    return 0;
}
