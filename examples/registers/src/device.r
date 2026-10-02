module example.registers.device;

std.string::string run(u32 initial, const u32[] writes) throws std.alloc::alloc_error {
    u32 register_value = initial;
    std.string::string output = std.string::create();
    unsafe {
        // This simulator uses live, aligned local storage. Volatile does not synchronize threads.
        raw u32* address = &register_value as raw u32*;
        raw const u32* input = &register_value as raw const u32*;
        for (const u32* value in &writes) {
            u32 before = core::volatile_load(input);
            core::volatile_store(address, *value);
            u32 after = core::volatile_load(input);
            std.string::string row = f"before={before} after={after}\n";
            str text = row.as_str();
            output.append(text);
        }
    }
    return move output;
}
