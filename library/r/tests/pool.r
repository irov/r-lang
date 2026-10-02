module tests.std.pool;
import std.test;
import std.pool;

// The tests of std.pool (Library R-SLIB-POOL-0001..0003): leases of a fixed set of objects, none
// when every object is lent out, objects returned by dropped leases and reused with their state,
// and leases taken and returned by several tasks. Run in test mode (Core R-FUNC-0025).

struct Buffer { bytes data; u32 uses; };

protected array<Buffer> buffers(u32 count) throws std.alloc::alloc_error {
    array<Buffer> made = [];
    for (u32 index = 0u32; index < count; index += 1u32) {
        try {
            made.push(Buffer {.data = std.bytes::with_capacity(64usize), .uses = 0u32});
        } catch (std.array::push_error<Buffer> rejected) {
            drop rejected;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    return move made;
}

@test
void lends_and_reuses_objects() throws std.test::failure, std.alloc::alloc_error {
    std.pool::pool<Buffer> objects = std.pool::pool<Buffer>::create(buffers(2u32));
    std.test::equal(objects.capacity(), 2usize);
    std.test::equal(objects.available(), 2usize);
    o<std.pool::lease<Buffer>> first = objects.acquire();
    o<std.pool::lease<Buffer>> second = objects.acquire();
    std.test::equal(objects.available(), 0usize);
    o<std.pool::lease<Buffer>> third = objects.acquire();
    switch (third) {
    case variant o::some(lease): std.test::check(false, "no object is free");
    case variant o::none: break;
    }
    switch (move first) {
    case variant o::some(move lease):
        {
            Buffer* item = lease.get_mut();
            item->uses += 1u32;
            item->data.append("abc");
        }
        drop lease;
    case variant o::none: std.test::check(false, "the first object");
    }
    std.test::equal(objects.available(), 1usize);
    o<std.pool::lease<Buffer>> again = objects.acquire();
    switch (again) {
    case variant o::some(lease):
        const Buffer* item = lease->get();
        std.test::equal(item->uses, 1u32);
        std.test::equal(len(item->data), 3usize);
    case variant o::none: std.test::check(false, "the returned object");
    }
    drop again;
    drop second;
    drop third;
    std.test::equal(objects.available(), 2usize);
}

protected async u32 borrow_many(std.pool::pool<Buffer> objects, u32 rounds) throws std.error::fault {
    u32 served = 0u32;
    for (u32 index = 0u32; index < rounds; index += 1u32) {
        o<std.pool::lease<Buffer>> taken = objects.acquire();
        switch (move taken) {
        case variant o::some(move lease):
            {
                Buffer* item = lease.get_mut();
                item->uses += 1u32;
            }
            served += 1u32;
            drop lease;
        case variant o::none: break;
        }
    }
    return served;
}

@test
async void serves_several_tasks() throws std.test::failure, std.error::fault {
    std.pool::pool<Buffer> objects = std.pool::pool<Buffer>::create(buffers(4u32));
    u32 total = 0u32;
    task_scope(3) workers {
        auto a = borrow_many(objects.share(), 200u32);
        auto b = borrow_many(objects.share(), 200u32);
        auto c = borrow_many(objects.share(), 200u32);
        total += await move a;
        total += await move b;
        total += await move c;
    }
    std.test::equal(total, 600u32);
    std.test::equal(objects.available(), 4usize);
}
