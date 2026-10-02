module test.codegen.async_null_type;

error Missing { Value, };

async i32 read(null_t absent) { return 1; }
async i32 read(i32 value) { return value; }
async void validate(null_t absent) throws Missing { throw Missing::Value; }

@generic<T: copy & send & unborrowed>
async i32 read_pair(null_t absent, T value) { return 2; }

@generic<T: copy & send & unborrowed>
async i32 nullable(own T*? pointer, T value) { i32 selected = pointer == null ? 12 : 13; return selected; }

struct Receipt { i32 missing; };
void Receipt::append(Receipt* this, null_t absent) { this->missing += 1; }
void Receipt::append(Receipt* this, i32 value) { this->missing += value; }

async i32 main() {
    try {
        i32 finalized = 0;
        try {
            i32 missing = await read(null);
            i32 present = await read(42);
            task<i32> operation = read((null));
            i32 named_task = await move operation;
            i32 generic = await read_pair(null, 42);
            if (missing != 1 || present != 42 || named_task != 1 || generic != 2) { throw TestAssertionFailed {.code = 1}; }
            own i32*? owner = null;
            i32 nullable_result = await nullable(move owner, 42);
            if (nullable_result != 12) { throw TestAssertionFailed {.code = 4}; }
            Receipt receipt = { .missing = 0 };
            receipt.append(null);
            receipt.append(2);
            if (receipt.missing != 3) { throw TestAssertionFailed {.code = 2}; }
            try {
                await validate(null);
                throw TestAssertionFailed {.code = 3};
            } catch (Missing failure) {
                finalized += 1;
            } finally { finalized += 1; }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 90}; }
        return finalized - 2;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
