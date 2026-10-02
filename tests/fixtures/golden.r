module golden;

error Error {
    i32 code;
};

protected i32 identity(i32 value) throws Error {
    try {
        if (value == 0) {
            throw {
                .code = value,
            };
        }
        return value;
    } catch (Error error) {
        throw;
    } finally {
        ;
    }
}

protected async i32 wait(task<i32 throws Error> operation) throws Error {
    i32 value = await move operation;
    return value;
}
