module test.codegen.static_addresses;

/* Core R-OBJ-0013 (B11): the address of a static object, of one of its fields or of an element at
   a constant index, converted to a raw pointer in the initializer of another static object, is an
   address constant. It needs no unsafe context, reads nothing and is relocated in the static
   image, so the pointers below hold their targets before main runs; writes through them reach
   the objects. */

struct Counters {
    u64 hits;
    u32[4] slots;
};

struct Node {
    raw const Node*? next;
    u32 value;
};

struct Route {
    raw u32* target;
    raw const u64* hits;
};

Counters state = {.hits = 5u64, .slots = {1u32, 2u32, 3u32, 4u32}};
raw u32* second = &state.slots[1] as raw u32*;
raw Counters* whole = &state as raw Counters*;
raw const u64* hits = &state.hits as raw const u64*;
Node tail = {.next = null, .value = 10u32};
Node head = {.next = &tail as raw const Node*, .value = 20u32};
(raw u32*)[3] table = {&state.slots[0] as raw u32*, &state.slots[2] as raw u32*,
                       &state.slots[3] as raw u32*};
Route route = {.target = &state.slots[3] as raw u32*, .hits = &state.hits as raw const u64*};

u32 last_of_pair() {
    static u32[2] pair = {7u32, 8u32};
    static raw u32* last = &pair[1] as raw u32*;
    unsafe {
        *last += 1u32;
        return pair[1];
    }
}

u32 walk() {
    u32 sum = 0u32;
    unsafe {
        raw const Node*? current = &head as raw const Node*;
        while (current != null) {
            sum += current->value;
            current = current->next;
        }
    }
    return sum;
}

i32 main() {
    unsafe {
        if ((*second != 2u32) || (whole->hits != 5u64) || (*hits != 5u64)) {
            return 1;
        }
        *second = 22u32;
        if (state.slots[1] != 22u32) {
            return 2;
        }
        whole->hits = 6u64;
        if (*hits != 6u64) {
            return 3;
        }
        *table[0] += 100u32;
        *table[2] += 300u32;
        if ((state.slots[0] != 101u32) || (*table[1] != 3u32) || (*route.target != 304u32)) {
            return 4;
        }
        if (*route.hits != 6u64) {
            return 5;
        }
    }
    if (walk() != 30u32) {
        return 6;
    }
    if ((last_of_pair() != 9u32) || (last_of_pair() != 10u32)) {
        return 7;
    }
    return 0;
}
