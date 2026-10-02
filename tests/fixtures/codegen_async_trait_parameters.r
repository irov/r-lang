module test.codegen.async_trait_parameters;

@generic<T: send & unborrowed>
trait Fetch : send & unborrowed {
    T resolve(const Self* this);
    async T produce(Self this) { return this.resolve(); }
};

struct Job { own i32* value; };
drop(Job* self) { *(self->value) = 9; }
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
struct Maker {};

impl Fetch<i32> for Job {
    i32 resolve(const Job* this) { return *(this->value); }
};
impl Fetch<Owner> for Maker {
    Owner resolve(const Maker* this) { return Owner {.value = new i32(42)}; }
};

async i32 main() {
    try {
        try {
            Job job = {.value = new i32(21)};
            i32 copied = await (move job).produce();
            Maker maker = {};
            Owner owned = await maker.produce();
            if (copied != 21 || *(owned.value) != 42) { throw TestAssertionFailed {.code = 1}; }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 90}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
