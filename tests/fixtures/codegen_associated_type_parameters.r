module test.codegen.associated_type_parameters;

/* R-TYPE-0045 (L16.3): associated types with type parameters. An application `P::Name<T>` stays
   abstract in a generic definition and becomes the binding at instantiation. */
trait Wrapper {
    type Of<T: copy>: copy;
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

@generic<W: Wrapper>
i32 roundtrip(W* wrapper, i32 value) {
    W::Of<i32> wrapped = wrapper->wrap_int(value);
    return wrapper->unwrap_int(wrapped);
}

/* A generic aggregate holds an application; a generic function passes one of its own parameter. */
@generic<W: Wrapper>
struct Holder { W::Of<i32> item; i32 extra; };

@generic<W: Wrapper>
i32 hold(W* wrapper, i32 value) {
    Holder<W> holder = Holder<W> {.item = wrapper->wrap_int(value), .extra = 1};
    return wrapper->unwrap_int(holder.item) + holder.extra;
}

@generic<W: Wrapper, T: copy>
W::Of<T> pass(W::Of<T> value) {
    return value;
}

/* A generic implementation binds through an application of another implementation. */
@generic<X: Wrapper>
struct Lift { X inner; };

@generic<X: Wrapper>
impl Wrapper for Lift<X> {
    type Of<T> = X::Of<T>;
    X::Of<i32> wrap_int(Lift<X>* this, i32 value) { return this->inner.wrap_int(value + 1); }
    i32 unwrap_int(Lift<X>* this, X::Of<i32> value) { return this->inner.unwrap_int(value); }
};

/* A default body, an owning binding and two parameters. */
trait Boxing {
    type Boxed<T>;
    Self::Boxed<i32> put(Self* this, i32 value);
    i32 take(Self* this, Self::Boxed<i32> boxed);
    i32 cycle(Self* this, i32 value) {
        Self::Boxed<i32> boxed = this->put(value);
        return this->take(move boxed);
    }
};

struct Heap { i32 bias; };

impl Boxing for Heap {
    type Boxed<T> = own T*;
    own i32* put(Heap* this, i32 value) { return new i32 (value + this->bias); }
    i32 take(Heap* this, own i32* boxed) {
        i32 value = *boxed;
        move boxed as void;
        return value;
    }
};

@generic<A, B>
struct Both { A left; B right; };

trait Joiner {
    type Joined<L, R>;
    Self::Joined<i32, bool> join(Self* this, i32 left, bool right);
    i32 score(Self* this, Self::Joined<i32, bool> joined);
};

struct Flip { i32 unused; };

impl Joiner for Flip {
    type Joined<L, R> = Both<R, L>;
    Both<bool, i32> join(Flip* this, i32 left, bool right) {
        return Both<bool, i32> {.left = right, .right = left};
    }
    i32 score(Flip* this, Both<bool, i32> joined) {
        if (joined.left == true) { return joined.right; }
        return 0;
    }
};

@generic<J: Joiner>
i32 join_pair(J* joiner) {
    J::Joined<i32, bool> value = joiner->join(4, true);
    return joiner->score(move value);
}

/* Nested applications and a parameter constraint that the binding body needs. */
trait Show {
    i32 shown(const Self* this);
};

struct Point { i32 x; };

impl Show for Point {
    i32 shown(const Point* this) { return this->x; }
};

@generic<T: Show>
struct Labeled { T value; i32 label; };

trait Family {
    type Member<T: Show>;
    Self::Member<Point> make(Self* this, Point point);
    i32 score(Self* this, const Self::Member<Point>* member);
};

struct Labeler { i32 next; };

impl Family for Labeler {
    type Member<T> = Labeled<T>;
    Self::Member<Point> make(Labeler* this, Point point) {
        this->next += 1;
        return Labeled<Point> {.value = point, .label = this->next};
    }
    i32 score(Labeler* this, const Self::Member<Point>* member) {
        return member->value.shown() + member->label;
    }
};

@generic<F: Family>
i32 labeled(F* family) {
    F::Member<Point> member = family->make(Point {.x = 40});
    i32 result = family->score(&member);
    move member as void;
    return result;
}

/* L16-1: a subtrait's method returns the associated type it inherits. */
trait Doubler: Wrapper {
    Self::Of<i32> doubled(Self* this, i32 value);
};

impl Doubler for Maybe {
    o<i32> doubled(Maybe* this, i32 value) { return o::some(value * 2); }
};

@generic<D: Doubler>
i32 use_doubler(D* doubler, i32 value) {
    D::Of<i32> twice = doubler->doubled(value);
    return doubler->unwrap_int(twice);
}

/* L16-2: a generic trait with an associated type with parameters. */
@generic<U: copy>
trait Converter {
    type Out<T: copy>;
    Self::Out<U> convert(Self* this, U value);
    U back(Self* this, Self::Out<U> value);
};

@generic<A: copy, B: copy>
struct Tagged { A value; B tag; };

struct Stamper { u8 stamp; };

impl Converter<i32> for Stamper {
    type Out<T> = Tagged<T, u8>;
    Tagged<i32, u8> convert(Stamper* this, i32 value) {
        return Tagged<i32, u8> {.value = value, .tag = this->stamp};
    }
    i32 back(Stamper* this, Tagged<i32, u8> value) { return value.value + value.tag as i32; }
};

@generic<C: Converter<i32>>
i32 converted(C* converter, i32 value) {
    C::Out<i32> out = converter->convert(value);
    return converter->back(move out);
}

/* L16-3: a receiver whose type holds no view gives an associated result nothing to hold. */
@generic<W: Wrapper & copy & unborrowed>
i32 by_value(W wrapper) {
    W local = wrapper;
    W::Of<i32> wrapped = local.wrap_int(5);
    return local.unwrap_int(wrapped);
}

i32 main() {
    Maybe maybe = Maybe {.fallback = 3};
    if (roundtrip(&maybe, 42) != 42) { return 1; }
    if (hold(&maybe, 20) != 21) { return 2; }
    o<i64> kept = pass::<Maybe, i64>(o::some(5i64));
    switch (move kept) {
    case variant o::some(move v):
        if (v != 5i64) { return 3; }
        break;
    case variant o::none:
        return 3;
    }
    Lift<Maybe> lift = Lift<Maybe> {.inner = Maybe {.fallback = 4}};
    if (hold(&lift, 30) != 32) { return 4; }
    Heap heap = Heap {.bias = 2};
    if (heap.cycle(5) != 7) { return 5; }
    Flip flip = Flip {.unused = 0};
    if (join_pair(&flip) != 4) { return 6; }
    Labeler labeler = Labeler {.next = 1};
    if (labeled(&labeler) != 42) { return 7; }
    if (use_doubler(&maybe, 7) != 14) { return 8; }
    Stamper stamper = Stamper {.stamp = 2u8};
    if (converted(&stamper, 40) != 42) { return 9; }
    if (by_value(maybe) != 5) { return 10; }
    return 0;
}
