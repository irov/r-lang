module test.semantic.attribute_repeated;

/* R-AGG-0013: a declaration carries each attribute type at most once. */
@attribute(field) struct key {};

struct Record {
    @key @key i32 value;
};

i32 main() { return 0; }
