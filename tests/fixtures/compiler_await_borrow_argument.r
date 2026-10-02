module compiler.await_borrow_argument;

/* R-BORROW-0024: the borrow of the first argument would stay live across the await of the
   second; the MIR check rejects the program with its own diagnostic. */
i32 add_to(i32* target, i32 amount) {
    *target += amount;
    return *target;
}

async i32 later() { return 3; }

async i32 main() {
    i32 total = 1;
    i32 seen = add_to(&total, await later());
    return seen - 4;
}
