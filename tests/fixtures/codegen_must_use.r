module test.codegen.must_use;
error Rejected {};
@must_use struct Ticket { own i32* data; };
drop(Ticket* self) { *(self->data) = 9; }
@must_use Ticket acquire(bool accepted) throws Rejected {
    throw (accepted == false) Rejected {};
    return Ticket {.data = new i32(7)};
}
@generic<T>
void discard(T value) { (move value) as void; }
@must_use i32 checksum() { return 42; }
i32 main() {
    i32 sum = checksum();
    discard(sum);
    if (sum != 42) { return 1; }
    i32 finalized = 0;
    try {
        Ticket first = acquire(true);
        discard(move first);
        acquire(true) as void;
        acquire(false) as void;
        return 2;
    } catch (Rejected failure) { finalized = 1; }
      finally { finalized += 10; }
    i32 selected = finalized == 11 ? 0 : 3;
    return selected;
}
