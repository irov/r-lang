module test.semantic.attribute_unplaced;

/* R-AGG-0013: an attribute of the program does not mark a function. */
@attribute(type) struct table { str name; };

@table("x") i32 helper() { return 0; }

i32 main() { return helper(); }
