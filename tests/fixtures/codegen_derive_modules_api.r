module fixture.derive.api;

/* A family whose members are derived in another module. */
error storage_error { i32 code; };
error disk_error : storage_error { u32 sector; };

@derive(clone, equal, ordered, key)
struct Label { u32 id; };
