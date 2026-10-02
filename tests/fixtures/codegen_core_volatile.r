module codegen.core_volatile;

i32 main() {
    i32 value = 7;
    i32 result = 1;
    unsafe {
        raw i32* write_address = &value as raw i32*;
        raw const i32* read_address = &value as raw const i32*;
        core::volatile_store(write_address, 41);
        i32 observed = core::volatile_load(read_address);
        if (observed == 41) {
            result = 0;
        }
    }
    return result;
}
