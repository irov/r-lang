module test.codegen.module_storage;

thread_local i32 module_counter = 5;
i32 shared_counter = 10;

i32 increment_local() {
    thread_local i32 local_counter = 1;
    local_counter += 1;
    return local_counter;
}

i32 increment_module() {
    module_counter += 2;
    const i32* current = &module_counter;
    return *current;
}

i32 increment_shared() {
    unsafe {
        shared_counter += 3;
        const i32* current = &shared_counter;
        return *current;
    }
}

i32 main() {
    i32 result = 0;
    result += increment_local() - 2;
    result += increment_local() - 3;
    result += increment_module() - 7;
    result += increment_module() - 9;
    result += increment_shared() - 13;
    result += increment_shared() - 16;
    return result;
}
