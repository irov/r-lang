module example.zip.tests.main;

import example.zip.error::{ZipError};
import example.zip.tests.archive::{run};

i32 main() {
    try {
        run();
        return 0;
    } catch (ZipError error) {
        error as void;
        return 1;
    }
}
