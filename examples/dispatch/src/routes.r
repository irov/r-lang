module example.dispatch.routes;
import std.deque;
import example.calculator.common::{Usage};

std.string::string route(const i32[] stops, bool returning, usize omit, bool cancel)
    throws Usage, std.array::push_error<i32>, std.alloc::alloc_error {
    std.deque::deque<i32> pending = std.deque::deque<i32>::create();
    for (const i32* stop in &stops) {
        if (returning == true) { pending.push_front(*stop); }
        else { pending.push_back(*stop); }
    }
    // Prepare the empty half before serving the route. Rebalancing assumes that half is empty.
    if (returning == true) { pending.rebalance_back(); }
    else { pending.rebalance_front(); }
    usize initial = pending.count();
    throw (omit > initial) Usage { .message = "cannot omit more stops than the route contains" };
    for (usize index = 0usize; index < omit; index += 1usize) {
        o<i32> removed = pending.pop_back(); removed as void;
    }
    if (cancel == true) { pending.clear(); }
    usize count = pending.count();
    bool empty = pending.is_empty();
    std.string::string output = f"stops={count} empty={empty}\n";
    o<const i32*> first = pending.front();
    switch (first) {
    case variant o::some(value):
        i32 stop = **value;
        std.string::string row = f"first={stop}\n";
        str view = row.as_str();
        output.append(view); break;
    case variant o::none: break;
    }
    o<const i32*> last = pending.back();
    switch (last) {
    case variant o::some(value):
        i32 stop = **value;
        std.string::string row = f"last={stop}\n";
        str view = row.as_str();
        output.append(view); break;
    case variant o::none: break;
    }
    output.append("route:");
    while (true) {
        o<i32> next = pending.pop_front();
        switch (next) {
        case variant o::some(value):
            i32 stop = *value;
            std.string::string row = f" {stop}";
            str view = row.as_str();
            output.append(view); break;
        case variant o::none:
            output.append("\n");
            return move output;
        }
    }
}
