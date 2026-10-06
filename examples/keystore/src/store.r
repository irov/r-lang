module example.keystore.store;

// One key-value interface for every backend. A `dyn(Store)*` borrows a backend of any type
// that implements the trait; each call dispatches to that type's implementation, and the
// compiler checks every backend the program converts to the interface (Core R-TYPE-0051).
error Full { u32 capacity; };

// A log entry; the latest live entry of a key holds its value.
struct Record { u32 key; u32 value; bool live; };

trait Store {
    constexpr str name(const Self* this);
    o<u32> get(const Self* this, u32 key);
    void put(Self* this, u32 key, u32 value) throws Full, std.array::push_error<Record>;
    bool remove(Self* this, u32 key);
    usize count(const Self* this);
};

enum Command { put, get, remove, count, backend };

usize arity(Command command) {
    switch (command) {
    case Command::put: return 2usize;
    case Command::get: fallthrough;
    case Command::remove: return 1usize;
    default: return 0usize;
    }
}

// Runs one command on whichever backend the interface borrows.
void apply(dyn(Store)* backend, Command command, u32 key, u32 value, std.string::string* output)
    throws Full, std.array::push_error<Record>, std.alloc::alloc_error {
    switch (command) {
    case Command::put:
        backend->put(key, value);
        std.string::string stored = f"stored {key}\n";
        output->append(stored); break;
    case Command::get:
        o<u32> found = backend->get(key);
        switch (found) {
        case variant o::some(current):
            u32 number = *current;
            std.string::string row = f"{key}={number}\n";
            output->append(row); break;
        case variant o::none:
            std.string::string missing = f"{key}=none\n";
            output->append(missing); break;
        }
        break;
    case Command::remove:
        bool removed = backend->remove(key);
        std.string::string report = f"removed={removed}\n";
        output->append(report); break;
    case Command::count:
        usize entries = backend->count();
        std.string::string total = f"count={entries}\n";
        output->append(total); break;
    case Command::backend:
        const dyn(Store)* view = backend;
        constexpr str label = view->name();
        output->append("backend=");
        output->append(label);
        output->append("\n"); break;
    }
}

// One parsed command.
struct Step { Command command; u32 key; u32 value; };

// Runs every command on whichever backend the interface borrows.
void run(dyn(Store)* backend, const Step[] steps, std.string::string* output)
    throws Full, std.array::push_error<Record>, std.alloc::alloc_error {
    for (usize index = 0usize; index < len(steps); index++) {
        Step step = steps[index];
        apply(backend, step.command, step.key, step.value, output);
    }
}
