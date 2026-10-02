module test.codegen.atomic_raw_modules_api;

/* R-TYPE-0013, R-MOD-0005: an exported struct field, module object and parameter of type
   `atomic raw T*?` cross the module interface with their atomic identity. */

struct Board {
    atomic raw i32*? latest;
    atomic raw const void*? tag;
};

atomic raw i32*? shared_cell = null;

Board make_board() {
    return Board {.latest = null, .tag = null};
}

bool board_empty(const Board* board) {
    unsafe {
        return core::atomic_load(&board->latest, core::memory_order::acquire) == null;
    }
}

void reset(const (atomic raw i32*?)* slot) {
    unsafe {
        core::atomic_store(slot, null, core::memory_order::release);
    }
}
