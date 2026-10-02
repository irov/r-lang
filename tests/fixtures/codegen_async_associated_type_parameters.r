module test.codegen.async_associated_type_parameters;

/* R-TYPE-0045 (L16.3): an async frame keeps values of applications of associated types across
   its awaits; its generic results and captures prove send and unborrowed through the
   constraints of the associated type. */
trait Wrapper {
    type Of<T: copy & send & unborrowed>: copy & send & unborrowed;
    Self::Of<i32> wrap_int(Self* this, i32 value);
    i32 unwrap_int(Self* this, Self::Of<i32> value);
};

struct Maybe { i32 fallback; };

impl Wrapper for Maybe {
    type Of<T> = o<T>;
    o<i32> wrap_int(Maybe* this, i32 value) { return o::some(value); }
    i32 unwrap_int(Maybe* this, o<i32> value) {
        switch (move value) {
        case variant o::some(move v): return v;
        case variant o::none: return this->fallback;
        }
    }
};

@generic<A: copy & send & unborrowed, B: copy & send & unborrowed>
struct Pair { A first; B second; };

struct Paired { i32 tag; };

impl Wrapper for Paired {
    type Of<T> = Pair<i32, T>;
    Pair<i32, i32> wrap_int(Paired* this, i32 value) {
        return Pair<i32, i32> {.first = this->tag, .second = value};
    }
    i32 unwrap_int(Paired* this, Pair<i32, i32> value) { return value.first + value.second; }
};

@generic<W: Wrapper & copy & send & unborrowed>
struct Holder { W::Of<i32> item; i32 extra; };

protected async i32 tick(i32 value) {
    return value + 1;
}

@generic<W: Wrapper & copy & send & unborrowed>
protected async i32 work(W wrapper) throws std.async::start_error {
    W local = wrapper;
    W::Of<i32> wrapped = local.wrap_int(10);
    i32 step = await tick(1);
    Holder<W> holder = Holder<W> {.item = wrapped, .extra = step};
    i32 later = await tick(step);
    return local.unwrap_int(holder.item) + holder.extra + later;
}

@generic<W: Wrapper & copy & send & unborrowed>
protected async W::Of<i32> produce(W wrapper, i32 value) throws std.async::start_error {
    W local = wrapper;
    i32 next = await tick(value);
    return local.wrap_int(next);
}

async i32 main() {
    try {
        Maybe maybe = Maybe {.fallback = 0};
        i32 first = await work(maybe);
        Paired paired = Paired {.tag = 100};
        i32 second = await work(paired);
        o<i32> produced = await produce(maybe, 6);
        Pair<i32, i32> pair = await produce(paired, 1);
        i32 status = 0;
        if (first != 15) { status = 1; }
        if (second != 115) { status = 2; }
        switch (move produced) {
        case variant o::some(move v):
            if (v != 7) { status = 3; }
            break;
        case variant o::none:
            status = 3;
            break;
        }
        if (pair.first + pair.second != 102) { status = 4; }
        return status;
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
