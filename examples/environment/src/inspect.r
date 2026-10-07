module example.environment.inspect;

std.string::string get(str name) throws std.env::env_error, std.alloc::alloc_error {
    // A pattern test in the condition binds the value of a present variable (R-STMT-0002).
    if (std.env::get(name) is variant o::some(move value)) {
        std.string::string output = f"{name}={value}\n";
        return move output;
    }
    std.string::string output = f"{name}=<absent>\n";
    return move output;
}

std.string::string arguments() throws std.env::env_error, std.alloc::alloc_error {
    array<std.string::string> snapshot = std.env::arguments();
    usize count = len(snapshot);
    std.string::string output = f"arguments={count}\n";
    usize index = 0usize;
    for (const std.string::string* argument in &snapshot) {
        str view = *argument;
        std.string::string row = f"{index}: {view}\n";
        str row_view = row;
        output.append(row_view);
        index += 1usize;
    }
    return move output;
}

std.string::string summary() throws std.env::env_error, std.alloc::alloc_error {
    // Report the snapshot size without disclosing unrelated environment values.
    dict<std.string::string, std.string::string> snapshot = std.env::variables();
    usize count = len(snapshot);
    std.string::string output = f"environment_entries={count}\n";
    return move output;
}
