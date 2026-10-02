module test.codegen.async_enum_cast_failure;

/* R-EXPR-0018 inside an async frame: an undeclared discriminant panics. */
@repr(C) enum Kind : c_int { Zero, One = 5, Two, };

async i32 main() {
    i64 undeclared = -1;
    Kind invalid = undeclared as Kind;
    return invalid as i32;
}
