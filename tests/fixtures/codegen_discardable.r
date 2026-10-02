module test.codegen.discardable;
error Rejected {};
struct Ticket { own i32* data; };
drop(Ticket* self) { *(self->data) = 9; }
@discardable Ticket acquire(bool accepted) throws Rejected {
    throw (accepted == false) Rejected {};
    return Ticket {.data = new i32(7)};
}
@discardable i32 Ticket::peek(const Ticket* this) { return *(this->data); }
trait Probe { @discardable i32 probe(const Self* this); };
impl Probe for Ticket { i32 probe(const Ticket* this) { return *(this->data) + 1; } };
@generic<T> @discardable T identity(T value) { return move value; }
@discardable i32 checksum() { return 42; }
i32 main() {
    i32 finalized = 0;
    checksum();
    identity(7);
    i32 kept = checksum();
    if (kept != 42) { return 1; }
    try {
        Ticket first = acquire(true);
        first.peek();
        first.probe();
        acquire(true);
        acquire(false);
        return 2;
    } catch (Rejected failure) { finalized = 1; }
      finally { finalized += 10; }
    i32 selected = finalized == 11 ? 0 : 3;
    return selected;
}
