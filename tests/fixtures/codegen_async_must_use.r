module test.codegen.async_must_use;
error Rejected {};
@must_use struct Ticket { own i32* data; };
drop(Ticket* self) { *(self->data) = 9; }
@must_use async Ticket acquire(bool accepted) throws Rejected {
    throw (accepted == false) Rejected {};
    return Ticket {.data = new i32(7)};
}
@generic<T: send & unborrowed>
async void discard(T value) { (move value) as void; }
async i32 main() {
    i32 finalized = 0;
    try {
        Ticket first = await acquire(true);
        await discard(move first);
        Ticket second = await acquire(true);
        (move second) as void;
        Ticket unexpected = await acquire(false);
        (move unexpected) as void;
        return 1;
    } catch (Rejected failure) { finalized = 1; }
      catch (std.async::start_error failure) { return 2; }
      finally { finalized += 10; }
    i32 selected = finalized == 11 ? 0 : 3;
    return selected;
}
