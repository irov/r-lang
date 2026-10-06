module test.semantic.attribute_unknown;

/* R-AGG-0013: a name that is no built-in attribute and no attribute type. */
struct Record {
    @unknown_attribute i32 value;
};

i32 main() { return 0; }
