module test.sync_initializer_cleanup_growth;

struct Value { own i32* payload; };
error Failure { i32 code; };

Value initialize() throws Failure {
    return Value { .payload = new i32(7) };
}

i32 main() {
    // Live owners add cleanup edges when a checked initializer is lowered.
    own i32* owner_0 = new i32(0);
    own i32* owner_1 = new i32(1);
    own i32* owner_2 = new i32(2);
    own i32* owner_3 = new i32(3);
    own i32* owner_4 = new i32(4);
    own i32* owner_5 = new i32(5);
    own i32* owner_6 = new i32(6);
    own i32* owner_7 = new i32(7);
    own i32* owner_8 = new i32(8);
    own i32* owner_9 = new i32(9);
    own i32* owner_10 = new i32(10);
    own i32* owner_11 = new i32(11);
    own i32* owner_12 = new i32(12);
    own i32* owner_13 = new i32(13);
    own i32* owner_14 = new i32(14);
    own i32* owner_15 = new i32(15);
    own i32* owner_16 = new i32(16);
    own i32* owner_17 = new i32(17);
    own i32* owner_18 = new i32(18);
    own i32* owner_19 = new i32(19);
    i32 total = 0;
    try {
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
        {
            std.sync::once_lock<Value> cell = std.sync::once_lock::<Value>();
            const Value* value = std.sync::get_or_init(&cell, initialize);
            total += *value->payload;
        }
    } catch (Failure failure) { return 1; }
    if (total != 252) { return 2; }
    return 0;
}
