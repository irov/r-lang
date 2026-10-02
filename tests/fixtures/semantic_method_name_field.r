module test.semantic.method_name_field;

/* R-NAME-0010: a method name differs from every field name of its owner. */
struct Point {
    i32 value;
};

i32 Point::value(const Point* this) {
    return this->value;
}

i32 main() {
    return 0;
}
