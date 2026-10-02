module test.trait_parameters;
@generic<T: copy>
trait Read {
    T read(const Self* this);
    T again(const Self* this) { return this->read(); }
};
struct Counter { i32 value; };
impl Read<i32> for Counter {
    i32 read(const Counter* this) { return this->value; }
};
@generic<T: Read<i32>>
i32 invoke(const T* value) { return value->again(); }
@generic<T: copy> struct Box { T value; };
@generic<T: copy> impl Read<T> for Box<T> {
    T read(const Box<T>* this) { return this->value; }
};
@generic<T: copy, U: Read<T>>
T read_with_context(const U* source, T fallback) { return source->again(); }
@generic<T: copy> trait Report : Read<T> {
    T repeated(const Self* this) { return this->read(); }
};
impl Report<i32> for Counter {};
@generic<T: copy> impl Report<T> for Box<T> {};
@generic<T: Report<i32>>
i32 read_report(const T* source) { return source->repeated() + source->read(); }
@generic<T: copy> trait Source { type Item: Read<T>; Self::Item make(const Self* this); };
struct Factory {};
impl Source<i32> for Factory {
    type Item=Counter;
    Counter make(const Factory* this) { return Counter {.value=42}; }
};
@generic<T: Source<i32>>
i32 read_factory(const T* factory) { T::Item item=factory->make(); return item.read(); }
struct Dual { i32 value; };
impl Read<i32> for Dual { i32 read(const Dual* this) { return this->value; } };
impl Read<u64> for Dual { u64 read(const Dual* this) { return this->value as u64; } };
@generic<T: Read<u64>> u64 unsigned_read(const T* source) { return source->again(); }
@generic<T> i32 read_if_available(const T* value) {
    @if (T is Read<i32>) { return value->read(); }
    @else { return 0; }
}
@generic<T: copy> trait PairRead : Read<T[2]> {};
@generic<T: copy> trait Leaf : PairRead<T> {};
struct PairSource { i32[2] values; };
impl Read<i32[2]> for PairSource {
    i32[2] read(const PairSource* this) { return this->values; }
};
impl PairRead<i32> for PairSource {};
impl Leaf<i32> for PairSource {};
@generic<T: Leaf<i32>> i32 pair_total(const T* value) {
    i32[2] pair=value->read();
    return pair[0]+pair[1];
}
i32 main() {
    Counter counter={.value=21};
    Box<i32> box={.value=42};
    if (read_with_context(&box, 0) != 42 || box.again() != 42) { return 1; }
    Factory factory={};
    Dual dual={.value=21};
    if (read_report(&counter) != 42 || read_factory(&factory) != 42) { return 2; }
    if (Counter::again(&counter) != 21 || Counter::read(&counter) != 21) { return 3; }
    if (invoke(&dual) != 21 || unsigned_read(&dual) != 21u64) { return 4; }
    PairSource pair={.values={20,22}};
    if (pair_total(&pair) != 42 || read_if_available(&counter) != 21) { return 5; }
    if (read_if_available(&factory) != 0) { return 6; }
    return invoke(&counter)+counter.again()-42;
}
