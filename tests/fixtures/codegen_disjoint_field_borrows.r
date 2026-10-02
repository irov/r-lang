module test.codegen.disjoint_field_borrows;

/* R-BORROW-0011: borrows of disjoint fields of one object coexist across statements, and an access
   through a borrow restricted to one field leaves the other fields available. */
struct Tracked { i32 value; };
struct Holder { Tracked item; Tracked other; };

void make(out Tracked result, i32 value) { result = Tracked {.value = value}; }
void bump(Tracked* t) { t->value += 1; }

i32 main() {
    Holder h = {.item = Tracked {.value = 1}, .other = Tracked {.value = 2}};
    Tracked* other = &h.other;
    Tracked* item = &h.item;
    item->value = 5;
    other->value = 6;
    if (h.item.value + h.other.value != 11) { return 1; }
    Tracked* second = &h.other;
    h.item.value = 7;
    bump(&h.item);
    second->value += 1;
    if (h.item.value != 8 || h.other.value != 7) { return 2; }
    // An out destination beside a live borrow of a sibling field.
    Tracked* kept = &h.other;
    make(out h.item, 20);
    kept->value = 30;
    if (h.item.value != 20 || h.other.value != 30) { return 3; }
    i32* a = &h.item.value;
    i32* b = &h.other.value;
    *a += 1;
    *b += 1;
    if (h.item.value != 21 || h.other.value != 31) { return 4; }
    // A borrow derived through a field borrow stays within that field.
    Tracked* through = &h.item;
    i32* inner = &through->value;
    h.other.value = 40;
    *inner = 50;
    if (h.item.value != 50 || h.other.value != 40) { return 5; }
    return 0;
}
