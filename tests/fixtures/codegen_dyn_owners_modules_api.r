module test.codegen.dyn_owners_api;

/* R-TYPE-0055 (L29): owners of an interface in the exported signatures and fields of another
   module; the consumer converts its own types to the interface. */

trait Shape { i32 area(const Self* this); void grow(Self* this); };

struct Square { i32 side; };
impl Shape for Square {
    i32 area(const Square* this) { return this->side * this->side; }
    void grow(Square* this) { this->side += 1; }
};

struct Holder { own dyn(Shape)* shape; };

own dyn(Shape)* square(i32 side) {
    own Square* made = new Square {.side = side};
    return move made;
}

i32 measure(own dyn(Shape)* s) { return s->area(); }
i32 shared_measure(arc dyn(Shape & send & sync) s) { return s->area(); }
