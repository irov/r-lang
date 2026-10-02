module test.codegen.opaque_results;

@generic<T: copy>
trait Read {
    T read(const Self* this);
    T again(const Self* this) { return this->read(); }
};

@generic<T: copy>
struct Box { T value; };
@generic<T: copy>
impl Read<T> for Box<T> {
    T read(const Box<T>* this) { return this->value; }
};

@generic<T: copy>
opaque(Read<T> & copy) boxed(T value) { return Box<T> {.value = value}; }
opaque(Read<i32> & copy) first(i32 value) { return Box<i32> {.value = value}; }
opaque(Read<i32> & copy) second(i32 value) { return Box<i32> {.value = value}; }

@generic<T: Read<i32>>
i32 read_value(const T* value) { return value->again(); }

@generic<T>
i32 count_calls(const T* value) {
    static i32 count = 0;
    unsafe {
        count += 1;
        return count;
    }
}

opaque(fn(i32) -> i32 & copy) adder(i32 offset) {
    fn i32 add(i32 value) move(offset) { return offset + value; }
    return add;
}

@generic<T: copy>
opaque(fn() -> T & copy) constant(T value) {
    fn T read() move(value) { return value; }
    return read;
}

struct View { const i32* value; };
impl Read<i32> for View {
    i32 read(const View* this) { return *(this->value); }
};
opaque(Read<i32> & copy) borrowed(const i32* value) { return View {.value = value}; }

struct Counter { i32 value; i32 limit; };
impl core::Iterator for Counter {
    type Item=i32;
    o<i32> next(Counter* this) {
        if (this->value >= this->limit) { return o::none; }
        i32 value=this->value;
        this->value += 1;
        return o::some(value);
    }
};
opaque(core::Iterator & Item = i32) count_to(i32 limit) {
    return Counter {.value=0, .limit=limit};
}

@generic<T: copy>
struct Singleton { o<T> value; };
@generic<T: copy>
impl core::Iterator for Singleton<T> {
    type Item=T;
    o<T> next(Singleton<T>* this) {
        o<T> value=this->value;
        this->value=o::none;
        return value;
    }
};
@generic<T: copy>
opaque(core::Iterator & Item = T) singleton(T value) {
    return Singleton<T> {.value=o::some(value)};
}

error Failure { i32 code; };
opaque(error) failure(i32 code) { return Failure {.code=code}; }
@generic<E: error>
i32 observe(E value) {
    try { throw move value; }
    catch (E caught) { return 42; }
}

i32 main() {
    auto a = first(20);
    auto b = second(22);
    if (read_value(&a) + read_value(&b) != 42) { return 1; }
    if (count_calls(&a) != 1 || count_calls(&a) != 2 || count_calls(&b) != 1) { return 2; }
    auto generic_value = boxed(42);
    if (generic_value.again() != 42) { return 3; }
    auto add = adder(20);
    auto read = constant(22);
    if (add(read()) != 42) { return 4; }
    auto counter=count_to(4);
    i32 total=0;
    for (i32 value in &counter) { total += value; }
    if (total != 6) { return 5; }
    auto single=singleton(42);
    i32 single_total=0;
    for (i32 value in &single) { single_total += value; }
    if (single_total != 42) { return 6; }
    auto error=failure(42);
    if (observe(move error) != 42) { return 7; }
    i32 source = 42;
    auto view = borrowed(&source);
    return view.read() - 42;
}
