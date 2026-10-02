module test.codegen.error_family_modules.base;

error base_failure { i32 code; };

/* A base value from another module still reads the fields of the base. */
i32 describe(base_failure failure) { return failure.code * 10; }
