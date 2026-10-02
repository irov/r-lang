module profile.freestanding_own;

/* R-CONF-0005: without an allocator the freestanding profile has no owners. */
protected void take(own u8* owner) {
    drop owner;
}

i32 main() {
    return 0;
}
