module test.codegen.out;
error Failed { i32 code; };
void produce(out i32 first, out own i32* second, bool fail) throws Failed {
    first = 20;
    second = new i32(22);
    if (fail == true) { throw Failed {.code=7}; }
}
void finalized(out i32 value) {
    value = 1;
    try { return; }
    finally { value += 41; }
}
@generic<T: unborrowed>
void assign(out T destination, T value) { destination = move value; }
@generic<F: fn(out i32) -> void>
void invoke(F operation, out i32 value) { operation(out value); }
opaque(fn(out i32) -> void & copy) factory() { return finalized; }
struct Source { i32 value; };
trait Produce { void get(const Self* this, out i32 value); };
impl Produce for Source {
    void get(const Source* this, out i32 value) { value = this->value; }
};
@generic<T: Produce>
void from_source(const T* source, out i32 value) { source->get(out value); }
void only_finally(out i32 value) { try { return; } finally { value = 42; } }
@discardable void optional(out i32 value) { value = 99; }
i32 count_drops(i32 change) {
    static i32 count = 0;
    unsafe { count += change; return count; }
}
struct Tracked { i32 value; };
drop(Tracked* self) { self->value as void; count_drops(1) as void; }
void tracked(out Tracked first, out Tracked second, i32 failure) throws Failed {
    first = Tracked {.value = 20};
    if (failure == 1) { throw Failed {.code = 1}; }
    second = Tracked {.value = 22};
    try {
        if (failure == 2) { throw Failed {.code = 2}; }
        return;
    } finally {
        if (failure == 3) {
            try { throw Failed {.code = 3}; }
            catch (Failed caught) { first.value = 19; second.value = 20 + caught.code; }
        }
    }
}
usize next_index(i32* count) { *count += 1; return 1usize; }
own i32* pair_with_return(out own i32* value) {
    value = new i32(20);
    return new i32(22);
}
void exceptional_finally(out Tracked value) throws Failed {
    value = Tracked {.value = 42};
    try { throw Failed {.code = 42}; }
    finally { drop value; }
}

i32 overloaded(out i32 value) { value = 40; return 2; }
i32 overloaded(i32* value) { return *value; }

i32 main() {
    try {
        i32 number = 3;
        own i32* owner = new i32(4);
        try { produce(out number, out owner, true); number as void; (move owner) as void; throw TestAssertionFailed {.code = 1}; }
        catch (Failed failure) { if (failure.code != 7 || number != 3 || *owner != 4) { throw TestAssertionFailed {.code = 2}; } }
        try { produce(out number, out owner, false); }
        catch (Failed failure) { failure as void; throw TestAssertionFailed {.code = 3}; }
        if (number + *owner != 42) { throw TestAssertionFailed {.code = 4}; }
        finalized(out number);
        if (number != 42) { throw TestAssertionFailed {.code = 5}; }
        assign(out number, 43);
        if (number != 43) { throw TestAssertionFailed {.code = 6}; }
        auto operation = finalized;
        operation(out number);
        if (number != 42) { throw TestAssertionFailed {.code = 7}; }
        invoke(finalized, out number);
        if (number != 42) { throw TestAssertionFailed {.code = 8}; }
        auto opaque_operation = factory();
        opaque_operation(out number);
        if (number != 42) { throw TestAssertionFailed {.code = 9}; }
        Source source = {.value = 42};
        from_source(&source, out number);
        if (number != 42) { throw TestAssertionFailed {.code = 10}; }
        only_finally(out number);
        if (number != 42) { throw TestAssertionFailed {.code = 11}; }
        i32 evaluations = 0;
        i32[2] items = {0, 0};
        finalized(out items[next_index(&evaluations)]);
        items as void;
        if (items[1] != 42 || evaluations != 1) { throw TestAssertionFailed {.code = 12}; }
        optional(out number);
        {
            Tracked first = {.value = 3};
            Tracked second = {.value = 4};
            try { tracked(out first, out second, 1); first.value as void; second.value as void; throw TestAssertionFailed {.code = 13}; }
            catch (Failed failure) {
                if (failure.code != 1 || first.value != 3 || second.value != 4 || count_drops(0) != 1) { throw TestAssertionFailed {.code = 14}; }
            }
            try { tracked(out first, out second, 2); first.value as void; second.value as void; throw TestAssertionFailed {.code = 15}; }
            catch (Failed failure) {
                if (failure.code != 2 || first.value != 3 || second.value != 4 || count_drops(0) != 3) { throw TestAssertionFailed {.code = 16}; }
            }
            try { tracked(out first, out second, 3); }
            catch (Failed failure) { failure as void; throw TestAssertionFailed {.code = 17}; }
            i32 first_value = first.value;
            i32 second_value = second.value;
            if (first_value != 19 || second_value != 23 || count_drops(0) != 5) { throw TestAssertionFailed {.code = 18}; }
            try { tracked(out first, out second, 0); }
            catch (Failed failure) { failure as void; throw TestAssertionFailed {.code = 19}; }
            if (first.value + second.value != 42 || count_drops(0) != 7) { throw TestAssertionFailed {.code = 20}; }
        }
        if (count_drops(0) != 9) { throw TestAssertionFailed {.code = 21}; }
        drop owner;
        assign(out owner, new i32(42));
        if (*owner != 42) { throw TestAssertionFailed {.code = 22}; }
        own i32* returned = pair_with_return(out owner);
        if (*returned + *owner != 42) { throw TestAssertionFailed {.code = 23}; }
        fn void closure(out i32 value) { value = 42; }
        invoke(closure, out number);
        if (number != 42) { throw TestAssertionFailed {.code = 24}; }
        {
            Tracked original = {.value = 3};
            try { exceptional_finally(out original); original.value as void; throw TestAssertionFailed {.code = 25}; }
            catch (Failed failure) {
                if (failure.code != 42 || original.value != 3 || count_drops(0) != 10) { throw TestAssertionFailed {.code = 26}; }
            }
        }
        if (count_drops(0) != 11) { throw TestAssertionFailed {.code = 27}; }
        i32 remainder = overloaded(out number);
        if (number + remainder != 42 || overloaded(&number) != 40) { throw TestAssertionFailed {.code = 28}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
