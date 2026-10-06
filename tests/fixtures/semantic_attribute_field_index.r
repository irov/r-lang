module test.semantic.attribute_field_index;

/* R-REFL-0005: a constant field index at or beyond the field count. */
@attribute(field) struct key {};
struct Record { @key i32 value; };

i32 main() {
    o<key> found = core::field_attribute::<key, Record>(4usize);
    if (found is variant o::some(_)) { return 1; }
    return 0;
}
