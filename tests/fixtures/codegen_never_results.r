module test.codegen.never_results;

/* R-TYPE-0007, R-FUNC-0003, R-EXPR-0015: functions with a never result, their calls as
   terminating statements and conditional arms, and the conversion of never to the type its
   context requires, in synchronous and asynchronous bodies. */

error Bad { i32 code; };

never fail(str message) { panic(message); }
never raise(i32 code) throws Bad { throw Bad {.code = code}; }
@generic<T> never fail_with(T value) {
    move value as void;
    panic("generic failure");
}
never forever() {
    while (true) {
    }
}

struct Door { i32 code; };
never Door::abort(const Door* this) {
    if (this->code > 0) { panic("positive"); }
    panic("other");
}

trait Halt {
    never halt(const Self* this);
    never refuse(const Self* this, i32 code) throws Bad;
};
struct Left { i32 bias; };
struct Right { i32 scale; };
impl Halt for Left {
    never halt(const Left* this) { panic("left"); }
    never refuse(const Left* this, i32 code) throws Bad { throw Bad {.code = code + this->bias}; }
};
impl Halt for Right {
    never halt(const Right* this) { panic("right"); }
    never refuse(const Right* this, i32 code) throws Bad { throw Bad {.code = code * this->scale}; }
};

i32 id(i32 x) { return x; }

i32 check(i32 x) {
    if (x < 0) {
        fail("negative");
        i32 unreachable = x + 1;
        return unreachable;
    }
    return x;
}
i32 checked(i32 x) throws Bad {
    if (x > 100) { raise(x); }
    return x;
}
i32 pick(i32 x) throws Bad {
    i32 y = x > 0 ? x : raise(x);
    return y;
}
i32 direct(i32 x) {
    if (x > 0) { return x; }
    return fail("direct");
}
i32 initialized(i32 x) {
    if (x > 0) { return x; }
    i32 value = panic("initialized");
    return value;
}
i32 argument(i32 x) throws Bad {
    if (x > 0) { return x; }
    return id(raise(x));
}
i32 operand(i32 x) {
    if (x > 0) { return x; }
    return 1 + fail("operand");
}
struct Pair { i32 first; i32 second; };
i32 assigned(i32 x) {
    i32 y = 1;
    if (x < 0) { y = fail("assigned"); }
    if (x < -10) { y += fail("compound"); }
    return y + x;
}
i32 aggregate(i32 x) {
    if (x > 0) { return x; }
    Pair pair = {.first = 1, .second = fail("field")};
    i32[2] items = {1, fail("element")};
    return pair.first + pair.second + items[0] + items[1];
}
@generic<T> T chosen(bool ok, T value) {
    if (ok == true) { return move value; }
    return fail("chosen");
}
i32 selected(i32 x) {
    switch (x) {
        case 1:
            return 10;
        default:
            return fail("switch");
    }
}
@generic<T: Halt> i32 bounded(const T* value, i32 x) {
    if (x < 0) { value->halt(); }
    return x;
}
i32 dispatched(const dyn(Halt)* value, i32 x) throws Bad {
    if (x < 0) { value->halt(); }
    if (x > 10) { value->refuse(x); }
    return x;
}

async i32 later(i32 x) throws Bad {
    if (x < 0) {
        raise(x);
        i32 unreachable = x + 1;
        return unreachable;
    }
    if (x > 1000) { fail("too large"); }
    return x + 1;
}
async i32 later_direct(i32 x) {
    if (x > 0) { return x; }
    return fail("later direct");
}
async i32 later_initialized(i32 x) {
    if (x > 0) { return x; }
    i32 value = fail("later initialized");
    return value;
}
async i32 later_argument(i32 x) throws Bad {
    if (x > 0) { return x; }
    return id(raise(x));
}
async i32 later_operand(i32 x) {
    if (x > 0) { return x; }
    return 1 + fail("later operand");
}
async i32 later_pick(i32 x) throws Bad {
    i32 y = x > 0 ? x : raise(x);
    return y;
}

i32 synchronous() {
    if (check(5) != 5 || direct(1) != 1 || initialized(2) != 2 || operand(3) != 3) { return 1; }
    if (selected(1) != 10 || assigned(2) != 3 || aggregate(3) != 3) { return 2; }
    if (chosen(true, 4) != 4) { return 2; }
    try {
        if (checked(7) != 7 || pick(8) != 8 || argument(9) != 9) { return 3; }
        checked(200) as void;
        return 4;
    } catch (Bad b) {
        if (b.code != 200) { return 5; }
    }
    try {
        pick(-4) as void;
        return 6;
    } catch (Bad b) {
        if (b.code != -4) { return 7; }
    }
    try {
        argument(-5) as void;
        return 8;
    } catch (Bad b) {
        if (b.code != -5) { return 9; }
    }
    Door door = {.code = 0};
    if (door.code != 0) { door.abort(); }
    if (door.code == 1) { fail_with(3); }
    if (door.code == 2) { forever(); }
    Left left = {.bias = 1};
    Right right = {.scale = 2};
    if (bounded(&left, 3) != 3) { return 10; }
    auto stop = fail;
    auto thrower = raise;
    if (door.code == 3) { stop("never"); }
    try {
        thrower(7);
    } catch (Bad b) {
        if (b.code != 7) { return 12; }
    }
    try {
        if (dispatched(&left, 6) != 6) { return 13; }
        dispatched(&right, 20) as void;
        return 14;
    } catch (Bad b) {
        if (b.code != 40) { return 15; }
    }
    try {
        dispatched(&left, 11) as void;
        return 16;
    } catch (Bad b) {
        if (b.code != 12) { return 17; }
    }
    return 0;
}

async i32 main() {
    const i32 status = synchronous();
    if (status != 0) { return status; }
    if (await later_direct(1) != 1) { return 20; }
    if (await later_initialized(2) != 2) { return 21; }
    if (await later_operand(3) != 3) { return 22; }
    try {
        if (await later(4) != 5) { return 23; }
        if (await later_argument(5) != 5) { return 24; }
        if (await later_pick(6) != 6) { return 25; }
        i32 value = await later(-6);
        value as void;
        return 26;
    } catch (Bad b) {
        if (b.code != -6) { return 27; }
    }
    try {
        i32 value = await later_argument(-7);
        value as void;
        return 28;
    } catch (Bad b) {
        if (b.code != -7) { return 29; }
    }
    try {
        i32 value = await later_pick(-8);
        value as void;
        return 30;
    } catch (Bad b) {
        if (b.code != -8) { return 31; }
    }
    return 0;
}
