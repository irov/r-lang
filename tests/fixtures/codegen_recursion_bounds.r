module test.codegen.recursion_bounds;

/* R-FUNC-0026 (L35): bounded recursion. A function with @recursion(depth = N) has at most N
   activations on a thread; the call that would begin one more throws core::recursion_error
   before its body runs, and every exit of an activation ends it. */

protected i32 drop_trace(i32 marker) {
    static i32 trace = 0;
    unsafe {
        if (marker != 0) { trace = trace + marker; }
        return trace;
    }
}

struct Resource { i32 marker; };

drop(Resource* self) {
    i32 ignored = drop_trace(self->marker);
    ignored as void;
}

@recursion(depth = 4)
protected u32 count_down(u32 value) throws core::recursion_error {
    if (value == 0u32) { return 0u32; }
    u32 below = count_down(value - 1u32);
    return below + 1u32;
}

/* Mutual recursion: each function counts its own activations; the prototype repeats the
   attribute of the definition. */
@recursion(depth = 3)
protected bool is_odd(u32 value) throws core::recursion_error;

@recursion(depth = 3)
protected bool is_even(u32 value) throws core::recursion_error {
    if (value == 0u32) { return true; }
    return is_odd(value - 1u32);
}

@recursion(depth = 3)
protected bool is_odd(u32 value) throws core::recursion_error {
    if (value == 0u32) { return false; }
    return is_even(value - 1u32);
}

/* Each instance of a generic function counts separately. */
@recursion(depth = 2)
@generic<T: copy>
protected T nest(T value, u32 levels) throws core::recursion_error {
    if (levels == 0u32) { return value; }
    return nest::<T>(value, levels - 1u32);
}

/* With depth 1, an activation of switch_instance<u16> calls switch_instance<u8>: the two
   instances count separately, so the inner call fits. */
@recursion(depth = 1)
@generic<T: copy>
protected T switch_instance(T value, bool descend) throws core::recursion_error {
    if (descend == true) {
        u8 inner = switch_instance::<u8>(1u8, false);
        inner as void;
    }
    return value;
}

/* The argument of a refused call is destroyed as on any throw. */
@recursion(depth = 2)
protected i32 consume(Resource resource, u32 levels) throws core::recursion_error {
    if (levels == 0u32) {
        drop resource;
        return 0;
    }
    i32 below = consume(Resource {.marker = resource.marker * 10}, levels - 1u32);
    drop resource;
    return below + 1;
}

/* A level that catches the refusal of a deeper call ends its own activation normally. */
@recursion(depth = 3)
protected u32 deepest(u32 level) throws core::recursion_error {
    u32 reached = level;
    try {
        u32 below = deepest(level + 1u32);
        if (below > reached) { reached = below; }
    } catch (core::recursion_error refused) {
        if (refused.depth != 3usize) { return 100u32; }
    }
    return reached;
}

/* Resource contracts and borrows hold across the recursion (L35-1). */
@noalloc @nonblocking @recursion(depth = 16)
protected u32 pure_depth(u32 n) throws core::recursion_error {
    if (n == 0u32) { return 0u32; }
    u32 below = pure_depth(n - 1u32);
    return below + 1u32;
}

@recursion(depth = 16)
protected const i32* largest(const i32[] items) throws core::recursion_error {
    if (len(items) == 1usize) { return &items[0]; }
    const i32* rest = largest(items[1..]);
    if (*rest > items[0]) { return rest; }
    return &items[0];
}

@recursion(depth = 16)
protected void bump(i32[] items) throws core::recursion_error {
    if (len(items) == 0usize) { return; }
    items[0] += 1;
    bump(items[1..]);
}

protected i32 contracts_and_borrows() {
    try {
        u32 depth = pure_depth(15u32);
        if (depth != 15u32) { return 61; }
        i32[4] values = {3, 9, 4, 7};
        bump(&values);
        const i32* top = largest(&values);
        if (*top != 10) { return 62; }
    } catch (core::recursion_error refused) {
        return 63;
    }
    return 0;
}

protected i32 self_recursion() {
    try {
        // count_down(3) needs four activations, exactly the depth.
        u32 three = count_down(3u32);
        if (three != 3u32) { return 1; }
    } catch (core::recursion_error refused) {
        return 3;
    }
    try {
        u32 four = count_down(4u32);
        four as void;
        return 2;
    } catch (core::recursion_error refused) {
        if (refused.depth != 4usize) { return 8; }
    }
    try {
        u32 refused_value = count_down(9u32);
        refused_value as void;
        return 4;
    } catch (core::recursion_error refused) {
        if (refused.depth != 4usize) { return 5; }
    }
    // The counter is back at zero: the full depth is available again.
    try {
        u32 again = count_down(3u32);
        if (again != 3u32) { return 6; }
    } catch (core::recursion_error refused) {
        return 7;
    }
    return 0;
}

protected i32 mutual_recursion() {
    try {
        bool four = is_even(4u32);
        if (four == false) { return 11; }
        bool five = is_even(5u32);
        if (five == true) { return 12; }
    } catch (core::recursion_error refused) {
        return 13;
    }
    try {
        bool deep = is_even(6u32);
        deep as void;
        return 14;
    } catch (core::recursion_error refused) {
        if (refused.depth != 3usize) { return 15; }
    }
    return 0;
}

protected i32 generic_instances() {
    try {
        u16 both = switch_instance::<u16>(5u16, true);
        if (both != 5u16) { return 21; }
        u16 two = nest::<u16>(2u16, 1u32);
        if (two != 2u16) { return 22; }
    } catch (core::recursion_error refused) {
        return 23;
    }
    try {
        u16 three = nest::<u16>(3u16, 2u32);
        three as void;
        return 24;
    } catch (core::recursion_error refused) {
        if (refused.depth != 2usize) { return 25; }
    }
    return 0;
}

protected i32 refused_arguments() {
    try {
        i32 fits = consume(Resource {.marker = 1}, 1u32);
        if (fits != 1) { return 31; }
    } catch (core::recursion_error refused) {
        return 32;
    }
    if (drop_trace(0) != 11) { return 33; }
    try {
        i32 refused_value = consume(Resource {.marker = 2}, 5u32);
        refused_value as void;
        return 34;
    } catch (core::recursion_error refused) {
        if (refused.depth != 2usize) { return 35; }
    }
    // 2 and 20 were held by the two activations, 200 by the refused call: all destroyed.
    if (drop_trace(0) != 233) { return 36; }
    return 0;
}

protected i32 caught_inside() {
    try {
        u32 reached = deepest(1u32);
        if (reached != 3u32) { return 41; }
        u32 again = deepest(1u32);
        if (again != 3u32) { return 42; }
    } catch (core::recursion_error refused) {
        return 43;
    }
    return 0;
}

protected async i32 in_task() {
    i32 result = self_recursion();
    return result;
}

async i32 main() {
    i32 first = self_recursion();
    if (first != 0) { return first; }
    i32 second = mutual_recursion();
    if (second != 0) { return second; }
    i32 third = generic_instances();
    if (third != 0) { return third; }
    i32 fourth = refused_arguments();
    if (fourth != 0) { return fourth; }
    i32 fifth = caught_inside();
    if (fifth != 0) { return fifth; }
    i32 contracts = contracts_and_borrows();
    if (contracts != 0) { return contracts; }
    i32 sixth = await in_task();
    if (sixth != 0) { return 50 + sixth; }
    return 0;
}
