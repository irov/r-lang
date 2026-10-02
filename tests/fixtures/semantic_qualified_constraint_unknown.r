module test.semantic.qualified_constraint_unknown;

/* R-TYPE-0043: a module-qualified trait constraint names a trait of a visible module. */
@generic<T: missing.other::Shown>
i32 describe(const T* value) {
    value as void;
    return 0;
}

i32 main() {
    i32 v = 1;
    i32 r = describe(&v);
    return r;
}
