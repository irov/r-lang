module test.semantic.attribute_field_type;

/* R-AGG-0013: a field of an attribute type holds a translation-time constant. */
@attribute(type) struct names { array<i32> list_of; };

i32 main() { return 0; }
