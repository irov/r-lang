module bench.struct_update;

/* Particles in an array of structs: position, velocity and a bounce, 10 000 steps of 4096. */
struct particle { i32 x; i32 y; i32 vx; i32 vy; };

i32 main() {
    array<particle> items = std.array::with_capacity::<particle>(4096usize);
    for (i32 i = 0; i < 4096; i += 1) {
        try { std.array::push(&items, particle { .x = i % 640, .y = (i * 7) % 480, .vx = (i % 5) - 2, .vy = (i % 7) - 3 }); }
        catch (std.array::push_error<particle> failure) { move failure as void; return 1; }
    }
    i64 check = 0i64;
    for (i32 step = 0; step < 10000; step += 1) {
        particle[] view = std.array::as_slice_mut(&items);
        for (usize i = 0usize; i < len(view); i += 1usize) {
            view[i].x += view[i].vx;
            view[i].y += view[i].vy;
            if ((view[i].x < 0) || (view[i].x >= 640)) { view[i].vx = -view[i].vx; }
            if ((view[i].y < 0) || (view[i].y >= 480)) { view[i].vy = -view[i].vy; }
        }
        check += (view[(step as usize) % len(view)].x as i64);
    }
    drop items;
    return (check % 109i64) as i32;
}
