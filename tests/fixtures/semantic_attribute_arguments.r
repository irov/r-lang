module test.semantic.attribute_arguments;

/* R-AGG-0013: arguments are all positional or all named, and a field without initializer needs
   one. */
@attribute(field) struct column { str name; i32 width = 0; };

struct Record {
    @column(width = 3) i32 value;
};

i32 main() { return 0; }
