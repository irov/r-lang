module test.semantic.attribute_declaration;

/* R-AGG-0013: @attribute names its targets from type, field and variant, each once. */
@attribute(type, type) struct table { str name; };

i32 main() { return 0; }
