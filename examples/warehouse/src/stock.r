module example.warehouse.stock;
import example.calculator.common::{Usage};

enum Command { set, add, get, has, remove, show, reserve, clear, reset };

usize arity(Command command) {
    switch (command) {
    case Command::set: fallthrough;
    case Command::add: return 2usize;
    case Command::get: fallthrough;
    case Command::has: fallthrough;
    case Command::remove: fallthrough;
    case Command::reserve: return 1usize;
    default: return 0usize;
    }
}

protected void append_value(std.string::string* output, str label, o<i32> result) throws std.alloc::alloc_error {
    switch (result) {
    case variant o::some(value):
        i32 number = *value;
        std.string::string row = f"{label}={number}\n";
        str row_text = row;
        output->append(row_text); break;
    case variant o::none:
        output->append(label);
        output->append("=none\n"); break;
    }
}

void apply(dict<i32, i32>* inventory, Command command, i32 key, i32 amount, std.string::string* output)
    throws Usage, std.dict::insert_error<i32, i32>, std.alloc::alloc_error {
    switch (command) {
    case Command::set:
        throw (amount < 0) Usage { .message = "inventory cannot be negative" };
        o<i32> previous = inventory->insert(key, amount);
        append_value(output, "previous", previous); break;
    case Command::add:
        o<i32*> found = inventory->get_mut(&key);
        switch (move found) {
        case variant o::some(pointer):
            o<i32> checked = core::checked_add(**pointer, amount);
            switch (checked) {
            case variant o::some(value):
                throw (*value < 0) Usage { .message = "insufficient inventory" };
                **pointer = *value;
                append_value(output, "quantity", checked); break;
            case variant o::none: throw Usage { .message = "inventory overflow" };
            }
            break;
        case variant o::none: output->append("missing\n"); break;
        }
        break;
    case Command::get:
        o<const i32*> found = inventory->get(&key);
        switch (found) {
        case variant o::some(pointer):
            o<i32> quantity = o::some(**pointer);
            append_value(output, "quantity", quantity); break;
        case variant o::none: output->append("missing\n"); break;
        }
        break;
    case Command::has:
        bool present = inventory->contains(&key);
        std.string::string row = f"present={present}\n";
        str row_text = row;
        output->append(row_text); break;
    case Command::remove:
        o<i32> removed = inventory->remove(&key);
        append_value(output, "removed", removed); break;
    case Command::reserve:
        throw (key < 0) Usage { .message = "reservation cannot be negative" };
        usize capacity = key as usize;
        inventory->reserve(capacity);
        output->append("reserved\n"); break;
    case Command::clear:
        inventory->clear();
        output->append("cleared\n"); break;
    case Command::reset:
        dict<i32, i32> empty = std.dict::create::<i32, i32>();
        *inventory = move empty;
        output->append("reset\n"); break;
    case Command::show:
        output->append("sku quantity\n");
        std.dict::iter<i32, i32> cursor = inventory->iter();
        while (true) {
            o<std.dict::entry_ref<i32, i32>> next = cursor.next();
            switch (next) {
            case variant o::some(entry):
                i32 sku = *entry->key;
                i32 quantity = *entry->value;
                std.string::string row = f"{sku} {quantity}\n";
                str row_text = row;
                output->append(row_text); break;
            case variant o::none: return;
            }
        }
        break;
    }
}
