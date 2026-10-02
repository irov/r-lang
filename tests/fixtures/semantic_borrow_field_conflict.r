module test.semantic.borrow_field_conflict;

/* R-BORROW-0002: a whole-object use of an aggregate keeps every borrow it carries alive, so
   an assignment to the exclusively borrowed origin in between conflicts. */
struct Slot {
    i32* target;
};

void Slot::bump(const Slot* this) {
    *(this->target) += 1;
}

i32 main() {
    i32 x = 1;
    Slot s = Slot { .target = &x };
    s.bump();
    x += 1;
    s.bump();
    return x;
}
