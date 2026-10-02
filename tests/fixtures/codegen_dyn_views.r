module test.codegen.dyn_views;

/* R-TYPE-0051, R-BORROW-0018: an interface borrow is a container element like any borrow, and an
   exclusive interface borrow is a function result derived from an exclusive input. */

trait Shape {
    i32 area(const Self* this);
    void grow(Self* this);
};

struct Square {
    i32 side;
};

struct Rect {
    i32 w;
    i32 h;
};

impl Shape for Square {
    i32 area(const Square* this) { return this->side * this->side; }
    void grow(Square* this) { this->side += 1; }
};

impl Shape for Rect {
    i32 area(const Rect* this) { return this->w * this->h; }
    void grow(Rect* this) { this->w += 1; }
};

i32 total(const array<const dyn(Shape)*>* shapes) {
    i32 sum = 0;
    for (const (const dyn(Shape)*)* shape in shapes) {
        sum += (*shape)->area();
    }
    return sum;
}

dyn(Shape)* larger(dyn(Shape)* left, dyn(Shape)* right) {
    if (left->area() >= right->area()) {
        return left;
    }
    return right;
}

i32 containers(Square* a, Rect* b) throws std.array::push_error<const dyn(Shape)*>,
    std.list::push_error<dyn(Shape)*>, std.dict::insert_error<i32, const dyn(Shape)*> {
    {
        array<const dyn(Shape)*> shapes = std.array::create::<const dyn(Shape)*>();
        std.array::push(&shapes, a);
        std.array::push(&shapes, b);
        if (total(&shapes) != 16) {
            return 1;
        }
        if (shapes[1]->area() != 12) {
            return 2;
        }
        dict<i32, const dyn(Shape)*> named = std.dict::create::<i32, const dyn(Shape)*>();
        std.dict::insert(&named, 7, a) as void;
        i32 key = 7;
        switch (std.dict::get(&named, &key)) {
            case variant o::some(found):
                if ((**found)->area() != 4) {
                    return 3;
                }
                break;
            case variant o::none:
                return 4;
        }
    }
    list<dyn(Shape)*> growing = std.list::create::<dyn(Shape)*>();
    std.list::push_back(&growing, b) as void;
    switch (std.list::front_mut(&growing)) {
        case variant o::some(front):
            (**front)->grow();
            break;
        case variant o::none:
            return 5;
    }
    return 0;
}

i32 main() {
    Square a = Square {.side = 2};
    Rect b = Rect {.w = 3, .h = 4};
    try {
        const i32 status = containers(&a, &b);
        if (status != 0) {
            return status;
        }
    } catch (std.array::push_error<const dyn(Shape)*> failure) {
        move failure as void;
        return 90;
    } catch (std.list::push_error<dyn(Shape)*> failure) {
        move failure as void;
        return 91;
    } catch (std.dict::insert_error<i32, const dyn(Shape)*> failure) {
        move failure as void;
        return 92;
    }
    if (b.w != 4) {
        return 6;
    }
    dyn(Shape)* big = larger(&a, &b);
    big->grow();
    if (b.w != 5) {
        return 7;
    }
    return 0;
}
