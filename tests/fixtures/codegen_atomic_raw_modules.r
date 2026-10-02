module test.codegen.atomic_raw_modules;
import test.codegen.atomic_raw_modules_api;

/* R-TYPE-0013, R-OBJ-0009: an importing module reaches the exported atomic raw pointers; an
   atomic operation on the mutable module object is safe, and borrowing it for a call is unsafe. */

i32 main() {
    test.codegen.atomic_raw_modules_api::Board board =
        test.codegen.atomic_raw_modules_api::make_board();
    if (test.codegen.atomic_raw_modules_api::board_empty(&board) == false) { return 1; }
    i32 value = 3;
    unsafe {
        core::atomic_store(&board.latest, &value as raw i32*, core::memory_order::release);
    }
    core::atomic_store(
        &test.codegen.atomic_raw_modules_api::shared_cell, null, core::memory_order::release);
    if (test.codegen.atomic_raw_modules_api::board_empty(&board) == true) { return 2; }
    test.codegen.atomic_raw_modules_api::reset(&board.latest);
    unsafe {
        test.codegen.atomic_raw_modules_api::reset(
            &test.codegen.atomic_raw_modules_api::shared_cell);
    }
    if (test.codegen.atomic_raw_modules_api::board_empty(&board) == false) { return 3; }
    return 0;
}
