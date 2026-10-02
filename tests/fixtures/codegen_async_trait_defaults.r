module test.codegen.async_trait_defaults;

error Failed { i32 code; };

trait Work : send & unborrowed {
    i32 read(const Self* this);
    async i32 execute(Self this) throws Failed {
        i32 value = this.read();
        throw (value < 0) Failed {.code = value};
        return value;
    }
};

struct Job { own i32* value; };
drop(Job* self) { *(self->value) = 9; }

impl Work for Job {
    i32 read(const Job* this) { return *(this->value); }
};

async i32 main() {
    try {
        Job success = {.value = new i32(42)};
        i32 result = await (move success).execute();
        if (result != 42) { return 1; }
        try {
            Job failure = {.value = new i32(-7)};
            i32 unexpected = await (move failure).execute();
            return unexpected;
        } catch (Failed error) {
            if (error.code != -7) { return 2; }
        }
    } catch (std.async::start_error failure) { return 90; }
      catch (Failed failure) { return 91; }
    return 0;
}
