module test.semantic.attribute_value;

/* R-AGG-0013: an argument has the type of its field, an integer within its range. */
@attribute(field) struct column { u8 width = 0u8; };

struct Record {
    @column(width = 300) i32 value;
};

i32 main() { return 0; }
