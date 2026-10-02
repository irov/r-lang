module test.codegen.async_const_generic_owners;
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }
@generic<T, const usize N>
struct Buffer { T[N] data; };
@generic<T, const usize N>
drop(Buffer<T, N>* self) { usize count = N; }
@generic<const usize N>
error Rejected { Buffer<Tracked, N> packet; };
@generic<const usize N>
async void reject(Buffer<Tracked, N> packet) throws Rejected<N> {
    throw Rejected<N> {.packet=move packet};
}
async i32 main() {
    Tracked[2] owners = {Tracked {.data=new i32(20)}, Tracked {.data=new i32(22)}};
    Buffer<Tracked, 2usize> packet = {.data=move owners};
    try { await reject(move packet); return 1; }
    catch (Rejected<2usize> failure) {
        i32 first = *(failure.packet.data[0].data);
        i32 second = *(failure.packet.data[1].data);
        if (first + second != 42) { return 2; }
    }
    catch (std.async::start_error failure) { return 3; }
    return 0;
}
