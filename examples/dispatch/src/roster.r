module example.dispatch.roster;
import std.set;
import std.sorted;
import std.cmp;

std.string::string reconcile(const i32[] requests, i32 withdrawn, bool cancel)
    throws std.dict::insert_error<i32, bool>, std.array::push_error<i32>, std.alloc::alloc_error {
    std.set::set<i32> arrival = std.set::set<i32>::create();
    std.sorted::set<i32> members = std.sorted::set<i32>::create();
    std.sorted::map<i32, i32> reservations = std.sorted::map<i32, i32>::create();
    for (const i32* requested in &requests) {
        i32 id = *requested;
        bool new_arrival = arrival.insert(id); new_arrival as void;
        bool new_member = members.insert(id); new_member as void;
        i32 amount = 1;
        o<const i32*> previous = reservations.get(&id);
        switch (previous) {
        case variant o::some(value): amount = **value + 1; break;
        case variant o::none: break;
        }
        o<i32> replaced = reservations.insert(id, amount); replaced as void;
    }
    bool registered = arrival.contains(&withdrawn);
    bool sorted_member = members.contains(&withdrawn);
    bool booked = reservations.contains(&withdrawn);
    usize position = members.lower_bound(&withdrawn);
    usize booking_position = reservations.lower_bound(&withdrawn);
    usize initial = members.count();
    std.string::string output = f"members={initial} registered={registered} sorted={sorted_member} booked={booked} position={position} booking_position={booking_position}\n";
    bool removed_arrival = arrival.remove(&withdrawn); removed_arrival as void;
    bool removed_member = members.remove(&withdrawn); removed_member as void;
    o<i32> removed = reservations.remove(&withdrawn);
    switch (removed) {
    case variant o::some(value):
        i32 amount = *value;
        std.string::string row = f"withdrawn={withdrawn} requests={amount}\n";
        str view = row;
        output.append(view); break;
    case variant o::none: break;
    }
    if (cancel == true) { arrival.clear(); }
    usize count = arrival.count();
    bool empty = arrival.is_empty();
    std.string::string summary = f"dispatchable={count} empty={empty}\narrival:";
    str summary_view = summary;
    output.append(summary_view);
    std.set::set_iter<i32> cursor = arrival.iter();
    bool done = false;
    while (true) {
        o<const i32*> next = cursor.next();
        switch (next) {
        case variant o::some(pointer):
            i32 id = **pointer;
            std.string::string row = f" {id}";
            str view = row;
            output.append(view); break;
        case variant o::none: done = true; break;
        }
        if (done == true) { break; }
    }
    output.append("\nbookings:");
    const i32[] ordered = members.as_slice();
    for (const i32* member in &ordered) {
        i32 id = *member;
        o<const i32*> amount = reservations.get(member);
        switch (amount) {
        case variant o::some(pointer):
            i32 quantity = **pointer;
            std.string::string row = f" {id}:{quantity}";
            str view = row;
            output.append(view); break;
        case variant o::none: break;
        }
    }
    usize bookings = reservations.count();
    std.string::string footer = f"\nbooking_count={bookings}\n";
    str footer_view = footer;
    output.append(footer_view);
    return move output;
}
