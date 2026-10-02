module test.codegen.enum_cast_failure;

/* R-EXPR-0018: an undeclared discriminant does not convert to the enum. */
@repr(C) enum Kind : c_int { Zero, One = 5, Two, };

i32 main() {
    i32 undeclared = 3;
    Kind invalid = undeclared as Kind;
    return invalid as i32;
}
