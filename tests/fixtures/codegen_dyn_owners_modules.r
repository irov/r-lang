module test.codegen.dyn_owners_modules;

/* R-TYPE-0055 (L29): a member type of the consumer module stored, called and destroyed through
   owners of an interface declared by the imported module. */

import test.codegen.dyn_owners_api;
import test.codegen.dyn_owners_api::{Shape, Holder};

struct Rect { i32 w; i32 h; };
impl Shape for Rect {
    i32 area(const Rect* this) { return this->w * this->h; }
    void grow(Rect* this) { this->w += 1; }
};

i32 main() {
    own Rect* r = new Rect {.w = 2, .h = 3};
    Holder holder = {.shape = move r};
    holder.shape->grow();
    i32 total = holder.shape->area();
    own dyn(Shape)* sq = test.codegen.dyn_owners_api::square(3);
    sq->grow();
    total += test.codegen.dyn_owners_api::measure(move sq);
    arc Rect shared = new arc Rect {.w = 1, .h = 5};
    total += test.codegen.dyn_owners_api::shared_measure(move shared);
    return total - (9 + 16 + 5);
}
