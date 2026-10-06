module test.semantic.attribute_target;

/* R-AGG-0013: an attribute type marks only its targets. */
@attribute(type) struct table { str name; };

struct Record {
    @table("x") i32 value;
};

i32 main() { return 0; }
