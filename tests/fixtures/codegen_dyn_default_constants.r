module test.codegen.dyn_default_constants;

/* R-TYPE-0050, R-TYPE-0051: a default method that reads an associated constant runs as the
   implementation of each member, so every member contributes its own constant. */
trait Store {
    const usize SLOTS;
    u32 slots(const Self* this) { return Self::SLOTS as u32; }
    u32 doubled(const Self* this) { return this->slots() * 2u32; }
};
struct Table { u32 v; };
struct Wide { u32 v; };
impl Store for Table { const usize SLOTS = 4usize; };
impl Store for Wide {
    const usize SLOTS = 8usize;
    u32 slots(const Wide* this) { return (Self::SLOTS as u32) + this->v; }
};

u32 via(const dyn(Store)* view) { return view->slots(); }
u32 twice(const dyn(Store)* view) { return view->doubled(); }

i32 main() {
    Table table = {.v = 1u32};
    Wide wide = {.v = 1u32};
    if (via(&table) != 4u32) { return 1; }
    if (via(&wide) != 9u32) { return 2; }
    if (twice(&table) != 8u32) { return 3; }
    if (twice(&wide) != 18u32) { return 4; }
    return 0;
}
