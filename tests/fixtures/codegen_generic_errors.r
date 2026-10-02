module test.codegen.generic_errors;

@generic<T> error Failure { T value; };

@generic<E: error>
protected void fail(E error) throws E { throw move error; }

@generic<T>
protected void reject(T value) throws Failure<T> {
    Failure<T> error = Failure<T> { .value = move value };
    try { fail(move error); }
    catch (Failure<T> caught) { throw; }
}

i32 main() {
    i32 finalized = 0;
    try { reject(7); }
    catch (Failure<i32> caught) { if (caught.value != 7) { return 1; } }
    finally { finalized += 1; }
    own i32* owner = new i32(11);
    try { reject(move owner); }
    catch (Failure<own i32*> caught) { if (*(caught.value) != 11) { return 2; } }
    finally { finalized += 1; }
    if (finalized != 2) { return 3; }
    return 0;
}
