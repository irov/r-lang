module profile.native_async_ok;

protected async i32 waiting(task<i32> work) {
    i32 value = await move work;
    return value;
}

async i32 main() {
    return 0;
}
