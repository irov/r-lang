module test.library.geometry;

/* B10 (R-FFI-0064): a library. Its exports are its API; geometry_initialize starts the runtime
   and the static objects below, geometry_shutdown destroys them and stops the runtime. */

@repr(C)
struct point {
    c_double x;
    c_double y;
};

@repr(C)
union number {
    c_int integer;
    c_double real;
};

@repr(C)
enum shape : c_int { circle = 1, square = 2 };

@repr(C)
struct flags {
    c_uint visible : 1;
    c_uint layer : 7;
};

/* Held by value: the header defines point before segment. */
@repr(C)
struct segment {
    point from;
    point to;
};

/* A pointer cycle: node is complete where its own pointer names it. */
@repr(C)
struct node {
    c_int value;
    raw node* next;
};

/* A packed struct inside an aligned one, an aligned member and an enum member. */
@repr(C, packed)
struct header {
    c_uchar tag;
    c_uint length;
};

@repr(C, align = 16)
struct block {
    header head;
    c_uchar[3] code;
    @align(8) c_ushort mark;
    shape kind;
};

/* A handle (R-FFI-0008): an R type without a C layout, which C holds as void * between the paired
   exports that make and end it. */
struct counter {
    u64 value;
    bytes note;
};

atomic u64 calls = 0u64;

/* A static object with an owner: initialized by geometry_initialize, destroyed by
   geometry_shutdown. */
bytes greeting = {};

protected void count() {
    core::atomic_fetch_add(&calls, 1u64, core::memory_order::relaxed) as void;
}

@export_name("geometry_segment_length_squared")
@safety("GEOMETRY-SEGMENT", "The library is initialized")
extern "C" c_double segment_length_squared(segment line) {
    count();
    c_double dx = line.to.x - line.from.x;
    c_double dy = line.to.y - line.from.y;
    return dx * dx + dy * dy;
}

@export_name("geometry_midpoint")
@safety("GEOMETRY-MIDPOINT", "The library is initialized")
extern "C" point midpoint(point left, point right) {
    count();
    return point { .x = (left.x + right.x) / (2.0 as c_double), .y = (left.y + right.y) / (2.0 as c_double) };
}

@export_name("geometry_scale")
@safety("GEOMETRY-SCALE", "The library is initialized and target points to a live point")
extern "C" void scale(raw point* target, c_double factor) {
    count();
    unsafe {
        target->x = target->x * factor;
        target->y = target->y * factor;
    }
}

@export_name("geometry_area")
@safety("GEOMETRY-AREA", "The library is initialized")
extern "C" c_double area(shape kind, c_double size) {
    count();
    if (kind == shape::square) {
        return size * size;
    }
    return (3.0 as c_double) * size * size;
}

@export_name("geometry_number_real")
@safety("GEOMETRY-NUMBER-REAL", "The library is initialized and value holds a real")
extern "C" c_double number_real(number value) {
    count();
    unsafe {
        return value.real;
    }
}

@export_name("geometry_flags_layer")
@safety("GEOMETRY-FLAGS-LAYER", "The library is initialized")
extern "C" c_uint flags_layer(flags value) {
    count();
    return value.layer;
}

@export_name("geometry_calls")
@safety("GEOMETRY-CALLS", "The library is initialized")
extern "C" c_uint64 total_calls() {
    return core::atomic_load(&calls, core::memory_order::relaxed) as c_uint64;
}

@export_name("geometry_node_sum")
@safety("GEOMETRY-NODE-SUM", "The library is initialized and first starts a chain of count live nodes")
extern "C" c_int node_sum(raw node* first, c_int length) {
    i32 total = 0;
    i32 remaining = length as i32;
    raw node* current = first;
    unsafe {
        while (remaining > 0) {
            total += current->value as i32;
            current = current->next;
            remaining -= 1;
        }
    }
    return total as c_int;
}

@export_name("geometry_block_length")
@safety("GEOMETRY-BLOCK", "The library is initialized")
extern "C" c_uint block_length(block value) {
    if (value.kind != shape::square) {
        return 0 as c_uint;
    }
    return value.head.length + (value.mark as c_uint) + (value.code[2] as c_uint);
}

@export_name("geometry_counter_new")
@safety("GEOMETRY-COUNTER-NEW", "The library is initialized; the result ends with geometry_counter_free")
extern "C" raw void*? counter_new() {
    try {
        own counter* owner = new counter { .value = 0u64, .note = {} };
        unsafe {
            raw counter* pointer = core::release(move owner);
            return pointer as raw void*;
        }
    } catch (std.alloc::alloc_error failure) {
        return null;
    }
}

@export_name("geometry_counter_add")
@safety("GEOMETRY-COUNTER-ADD", "handle is a live result of geometry_counter_new")
extern "C" c_uint64 counter_add(raw void* handle, c_uint64 amount) {
    unsafe {
        raw counter* target = handle as raw counter*;
        target->value = target->value + (amount as u64);
        return target->value as c_uint64;
    }
}

@export_name("geometry_counter_free")
@safety("GEOMETRY-COUNTER-FREE", "handle is a live result of geometry_counter_new, ended here")
extern "C" void counter_free(raw void* handle) {
    unsafe {
        own counter* owner = core::adopt(handle as raw counter*);
        drop owner;
    }
}
