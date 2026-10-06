module test.semantic.attribute_reflection_operand;

/* R-REFL-0005: the first operand of attribute reflection is an attribute type with the target. */
@attribute(field) struct key {};
struct Record { i32 value; };

i32 main() {
    o<key> found = core::type_attribute::<key, Record>();
    if (found is variant o::some(_)) { return 1; }
    return 0;
}
