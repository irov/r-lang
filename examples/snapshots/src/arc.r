module example.snapshots.atomic_ref;

struct Snapshot {
    i32 version;
    i32 value;
};

struct Delivery { o<arc Snapshot> pending; };

std.string::string run(i32 initial, i32 revised, bool retain_reader) throws std.alloc::alloc_error {
    arc Snapshot published = new arc Snapshot { .version = 1, .value = initial };
    i32 previous_version = 0;
    i32 previous_value = 0;
    Snapshot*? editable = published.get_mut();
    if (editable != null) {
        Snapshot previous = core::replace(&*editable, Snapshot { .version = 2, .value = revised });
        previous_version = previous.version;
        previous_value = previous.value;
    }
    weak arc Snapshot subscription = published.downgrade();
    weak arc Snapshot observer = subscription.clone();
    arc Snapshot reader = published.clone();
    bool same = published.ptr_eq(&reader);
    usize readers = published.strong_count();
    usize observers = published.weak_count();
    std.string::string output = f"replaced version={previous_version} value={previous_value}\nsame_snapshot={same} owners={readers} observers={observers}\n";

    // The token holds one strong reference while passing through an opaque foreign interface.
    arc Snapshot exported = published.clone();
    raw const Snapshot* token = (move exported).into_raw();
    unsafe {
        // Consume exactly the token produced above, once, with the same concrete type.
        arc Snapshot returned = std.arc::from_raw(token);
        i32 version = returned->version;
        i32 value = returned->value;
        std.string::string row = f"foreign_snapshot version={version} value={value}\n";
        str row_text = row.as_str();
        output.append(row_text);
    }
    o<arc Snapshot> refreshed = subscription.upgrade();
    Delivery delivery = { .pending = move refreshed };
    o<arc Snapshot> delivered = core::take(&delivery.pending);
    switch (move delivered) {
    case variant o::some(move current):
        i32 value = current->value;
        std.string::string row = f"subscription={value}\n";
        str row_text = row.as_str();
        output.append(row_text);
        break;
    case variant o::none: output.append("subscription=expired\n"); break;
    }
    drop observer;
    drop reader;
    o<arc Snapshot> keeper = o::none;
    if (retain_reader == true) {
        arc Snapshot kept = published.clone();
        keeper = o::some(move kept);
    }
    std.arc::try_unwrap_result<Snapshot> outcome = (move published).try_unwrap();
    switch (move outcome) {
    case variant std.arc::try_unwrap_result::unwrapped(move value):
        std.string::string row = f"unwrapped version={value.version} value={value.value}\n";
        str row_text = row.as_str();
        output.append(row_text);
        break;
    case variant std.arc::try_unwrap_result::shared(move retained):
        i32 value = retained->value;
        std.string::string row = f"still_shared value={value}\n";
        str row_text = row.as_str();
        output.append(row_text);
        break;
    }
    o<arc Snapshot> remaining = subscription.upgrade();
    bool alive = false;
    switch (move remaining) {
    case variant o::some(move value): alive = true; break;
    case variant o::none: break;
    }
    std.string::string row = f"reader_keeps_alive={alive}\n";
    str row_text = row.as_str();
    output.append(row_text);
    return move output;
}
