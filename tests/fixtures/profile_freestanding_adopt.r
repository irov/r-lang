module profile.freestanding_adopt;

/* R-CONF-0005: core::adopt needs the allocator that releases the adopted object. */
protected i32 take(raw u8* pointer) {
    unsafe {
        own u8* owner = core::adopt(pointer);
        drop owner;
    }
    return 0;
}

i32 main() {
    return 0;
}
