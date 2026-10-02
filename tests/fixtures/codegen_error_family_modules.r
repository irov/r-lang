module test.codegen.error_family_modules;

import test.codegen.error_family_modules.base;

/* L21.1: the parent of an error may be declared by another module; the family is closed when the
   program is built, so a catch of the imported base receives the error of this module
   (R-AGG-0011). */
error parse_failure : test.codegen.error_family_modules.base::base_failure { i32 line; };

void load(i32 kind) throws test.codegen.error_family_modules.base::base_failure {
    throw (kind == 1) parse_failure {.code = 1, .line = 12};
    throw (kind == 2) test.codegen.error_family_modules.base::base_failure {.code = 2};
}

i32 main() {
    i32 failures = 0;
    for (i32 kind = 1; kind <= 2; kind += 1) {
        try {
            load(kind);
        } catch (test.codegen.error_family_modules.base::base_failure e) {
            failures += e.code == kind ? 0 : 1;
            failures += test.codegen.error_family_modules.base::describe(e) == kind * 10 ? 0 : 2;
            try {
                throw;
            } catch (parse_failure p) {
                failures += p.line == 12 && kind == 1 ? 0 : 4;
            } catch (test.codegen.error_family_modules.base::base_failure other) {
                failures += kind == 2 ? 0 : 8;
            }
        }
    }
    return failures;
}
