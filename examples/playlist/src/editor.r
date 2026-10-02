module example.playlist.editor;

enum Command {
    append, prepend, before, after, set, set_first, set_last,
    first, last, get, remove, pop_first, pop_last, clear, show,
};

usize arity(Command command) {
    switch (command) {
    case Command::before: fallthrough; case Command::after: fallthrough; case Command::set: return 2usize;
    case Command::append: fallthrough; case Command::prepend: fallthrough; case Command::set_first: fallthrough; case Command::set_last: fallthrough;
    case Command::get: fallthrough; case Command::remove: return 1usize;
    default: return 0usize;
    }
}

protected void append_number(std.string::string* output, str label, i32 number) throws std.alloc::alloc_error {
    std.string::string row = f"{label}={number}\n";
    str row_text = row.as_str();
    output->append(row_text);
}

void apply(list<i32>* tracks, Command command, usize index, i32 value, std.string::string* output)
    throws std.list::push_error<i32>, std.alloc::alloc_error {
    switch (command) {
    case Command::append:
        i32* inserted = tracks->push_back(value);
        append_number(output, "appended", *inserted); break;
    case Command::prepend:
        i32* inserted = tracks->push_front(value);
        append_number(output, "prepended", *inserted); break;
    case Command::before: fallthrough;
    case Command::after:
        o<const i32*> anchor = tracks->get(index);
        switch (anchor) {
        case variant o::some(pointer):
            const i32* position = *pointer;
            if (command == Command::before) {
                i32* inserted = tracks->insert_before(position, value);
                append_number(output, "inserted", *inserted);
            } else {
                i32* inserted = tracks->insert_after(position, value);
                append_number(output, "inserted", *inserted);
            }
            break;
        case variant o::none: output->append("missing\n"); break;
        }
        break;
    case Command::set: fallthrough;
    case Command::set_first: fallthrough;
    case Command::set_last: fallthrough;
    case Command::remove:
        o<i32*> editable = o::none;
        if (command == Command::set_first) { editable = tracks->front_mut(); }
        else {
            if (command == Command::set_last) { editable = tracks->back_mut(); }
            else { editable = tracks->get_mut(index); }
        }
        switch (move editable) {
        case variant o::some(pointer):
            if (command == Command::remove) {
                i32* node = *pointer;
                i32 removed = tracks->remove(move node);
                append_number(output, "removed", removed);
            } else {
                **pointer = value;
                append_number(output, "set", **pointer);
            }
            break;
        case variant o::none: output->append("missing\n"); break;
        }
        break;
    case Command::first: fallthrough;
    case Command::last: fallthrough;
    case Command::get:
        o<const i32*> selected = o::none;
        if (command == Command::first) { selected = tracks->front(); }
        else {
            if (command == Command::last) { selected = tracks->back(); }
            else { selected = tracks->get(index); }
        }
        switch (selected) {
        case variant o::some(pointer): append_number(output, "track", **pointer); break;
        case variant o::none: output->append("missing\n"); break;
        }
        break;
    case Command::pop_first: fallthrough;
    case Command::pop_last:
        o<i32> removed = o::none;
        if (command == Command::pop_first) { removed = tracks->pop_front(); }
        else { removed = tracks->pop_back(); }
        switch (removed) {
        case variant o::some(number): append_number(output, "removed", *number); break;
        case variant o::none: output->append("missing\n"); break;
        }
        break;
    case Command::clear:
        tracks->clear();
        output->append("cleared\n"); break;
    case Command::show:
        output->append("tracks");
        std.list::iter<i32> cursor = tracks->iter();
        while (true) {
            o<const i32*> next = cursor.next();
            switch (next) {
            case variant o::some(pointer):
                i32 number = **pointer;
                std.string::string cell = f" {number}";
                str cell_text = cell.as_str();
                output->append(cell_text); break;
            case variant o::none: output->append("\n"); return;
            }
        }
        break;
    }
}
