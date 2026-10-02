module compiler.missing_import;

/* R-MOD-0007: an import that no module map entry declares is diagnosed at the import. */
import compiler.absent;

i32 main() { return 0; }
