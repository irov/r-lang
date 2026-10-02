module test.codegen.trait_defaults;
struct Point { i32 value; };
trait Measure { i32 measure(const Self* this); };
trait Report : Measure {
  i32 twice(const Self* this) { return this->measure() + this->measure(); }
  i32 twice(const Self* this, i32 adjustment) { return this->twice() + adjustment; }
};
trait Source {
  type Item: copy;
  Self::Item read(const Self* this);
  Self::Item reread(const Self* this) { Self::Item value=this->read(); return value; }
};
impl Measure for Point { i32 measure(const Point* this) { return this->value; } };
impl Report for Point {};
impl Source for Point { type Item=i32; i32 read(const Point* this) { return this->value; } };
@generic<T: Report>
i32 invoke(const T* value) { return value->measure() + value->twice(); }
@generic<T: copy>
struct Box { T value; };
@generic<T: copy>
impl Source for Box<T> {
    type Item=T;
    T read(const Box<T>* this) { return this->value; }
};
trait Detailed : Source {
    Self::Item repeated(const Self* this) { return this->reread(); }
};
impl Detailed for Point {};
impl Report for i32 {
    i32 twice(const i32* this) { return *this + 1; }
};
impl Measure for i32 { i32 measure(const i32* this) { return *this; } };
i32 main() {
    Point p={.value=14};
    Box<i32> box={.value=42};
    i32 scalar=4;
    if (box.reread() != 42 || p.repeated() != 14 || scalar.twice() != 5 || scalar.twice(37) != 42 || p.twice(14) != 42) { return 1; }
    return invoke(&p) + p.reread() - 56;
}
