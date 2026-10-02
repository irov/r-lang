module test.codegen.module_storage_import;

import test.codegen.module_storage_api::{exported_shared_counter, exported_thread_counter};

i32 main() {
    exported_thread_counter += 3;
    if (exported_thread_counter != 7) {
        return 1;
    }
    unsafe {
        exported_shared_counter += 5;
        if (exported_shared_counter != 13) {
            return 2;
        }
    }
    return 0;
}
