module test.regression.atomic_glue_unused_drop;

/* M26-1: an atomic field needs move glue when its owner is shared through an arc, but no drop
   glue: its owner has a drop hook of its own. The drop function of the atomic type was emitted
   anyway and failed -Wunused-function. */
struct Slot { atomic raw void*? handle; u32 id; };

drop(Slot* self) {
    raw void*? value = core::atomic_load(&self->handle, core::memory_order::relaxed);
    value as void;
}

protected async u32 keep(Slot value) {
    return value.id;
}

protected u32 shared_id(arc Slot shared) { return shared->id; }

async i32 main() {
    Slot first = Slot {.handle = null, .id = 7u32};
    u32 id = await keep(move first);
    arc Slot shared = new arc Slot {.handle = null, .id = 3u32};
    o<arc Slot> kept = o::some(std.arc::clone(&shared));
    drop kept;
    u32 other = shared_id(move shared);
    return (id as i32) + (other as i32) - 10;
}
