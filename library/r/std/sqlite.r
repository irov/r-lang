module std.sqlite;

/* R-SLIB-SQLITE-0001: the R part of std.sqlite over its native provider std.sqlite.native, which
   calls the SQLite library built with the standard library. A connection is opened in the
   serialized threading mode; every operation that reaches the database runs on the blocking call
   pool (R-SLIB-ASYNC-0017) and never on an executor worker. */

@link(name = "std.sqlite.native", kind = "static")
@header("r_std_sqlite_native.h")
extern "C" {
    @safety("SQLITE-OPEN", "path addresses path_length bytes and database is writable")
    c_int32 r_std_sqlite_native_open(raw const c_uint8*? path,
                                     c_size path_length,
                                     c_int32 flags,
                                     c_int32 busy_timeout,
                                     raw (raw void*?)* database);

    @safety("SQLITE-CLOSE", "database is null or a handle of open that ends with this call")
    void r_std_sqlite_native_close(raw void*? database);

    @safety("SQLITE-DATABASE-MESSAGE", "database is live and target addresses capacity writable bytes")
    c_size r_std_sqlite_native_database_message(raw void*? database,
                                                raw c_uint8*? target,
                                                c_size capacity);

    @safety("SQLITE-STATEMENT-MESSAGE", "statement is live and target addresses capacity writable bytes")
    c_size r_std_sqlite_native_statement_message(raw void*? statement,
                                                 raw c_uint8*? target,
                                                 c_size capacity);

    @safety("SQLITE-EXECUTE", "database is live and sql addresses length bytes")
    c_int32 r_std_sqlite_native_execute(raw void*? database, raw const c_uint8*? sql, c_size length);

    @safety("SQLITE-PREPARE",
            "database is live, sql addresses length bytes and statement and consumed are writable")
    c_int32 r_std_sqlite_native_prepare(raw void*? database,
                                        raw const c_uint8*? sql,
                                        c_size length,
                                        raw (raw void*?)* statement,
                                        raw c_size* consumed);

    @safety("SQLITE-AUTOCOMMIT", "database is live")
    c_int32 r_std_sqlite_native_autocommit(raw void*? database);

    @safety("SQLITE-FINALIZE", "statement is null or a handle of prepare that ends with this call")
    void r_std_sqlite_native_finalize(raw void*? statement);

    @safety("SQLITE-PARAMETERS", "statement is live")
    c_int32 r_std_sqlite_native_parameter_count(raw void*? statement);

    @safety("SQLITE-BIND-NULL", "statement is live")
    c_int32 r_std_sqlite_native_bind_null(raw void*? statement, c_int32 index);

    @safety("SQLITE-BIND-INTEGER", "statement is live")
    c_int32 r_std_sqlite_native_bind_integer(raw void*? statement, c_int32 index, c_int64 value);

    @safety("SQLITE-BIND-REAL", "statement is live")
    c_int32 r_std_sqlite_native_bind_real(raw void*? statement, c_int32 index, c_double value);

    @safety("SQLITE-BIND-TEXT", "statement is live and data addresses length bytes, which are copied")
    c_int32 r_std_sqlite_native_bind_text(raw void*? statement,
                                          c_int32 index,
                                          raw const c_uint8*? data,
                                          c_size length);

    @safety("SQLITE-BIND-BLOB", "statement is live and data addresses length bytes, which are copied")
    c_int32 r_std_sqlite_native_bind_blob(raw void*? statement,
                                          c_int32 index,
                                          raw const c_uint8*? data,
                                          c_size length);

    @safety("SQLITE-STEP", "statement is live and changes and last_row_id are writable")
    c_int32 r_std_sqlite_native_step(raw void*? statement,
                                     raw c_int64* changes,
                                     raw c_int64* last_row_id);

    @safety("SQLITE-RESET", "statement is live")
    void r_std_sqlite_native_reset(raw void*? statement);

    @safety("SQLITE-COLUMN-COUNT", "statement is live")
    c_int32 r_std_sqlite_native_column_count(raw void*? statement);

    @safety("SQLITE-COLUMN-TYPE", "statement is live")
    c_int32 r_std_sqlite_native_column_type(raw void*? statement, c_int32 column);

    @safety("SQLITE-COLUMN-INTEGER", "statement is live")
    c_int64 r_std_sqlite_native_column_integer(raw void*? statement, c_int32 column);

    @safety("SQLITE-COLUMN-REAL", "statement is live")
    c_double r_std_sqlite_native_column_real(raw void*? statement, c_int32 column);

    @safety("SQLITE-COLUMN-BYTES", "statement is live and target addresses capacity writable bytes")
    c_size r_std_sqlite_native_column_bytes(raw void*? statement,
                                            c_int32 column,
                                            raw c_uint8*? target,
                                            c_size capacity);

    @safety("SQLITE-COLUMN-NAME", "statement is live and target addresses capacity writable bytes")
    c_size r_std_sqlite_native_column_name(raw void*? statement,
                                           c_int32 column,
                                           raw c_uint8*? target,
                                           c_size capacity);
}

/* The status of the provider for a zero byte in a path or in SQL text. */
protected const i32 EMBEDDED_ZERO = -2i32;

/* The address of the first byte of a view as the provider reads it, null for an empty view. */
protected raw const c_uint8*? bytes_of(const u8[] data) {
    if (len(data) == 0usize) { return null; }
    unsafe {
        raw const u8* first = &data[0usize] as raw const u8*;
        raw const void* erased = first as raw const void*;
        return erased as raw const c_uint8*;
    }
}

/* The address of the first byte of a view as the provider writes it, null for an empty view. */
protected raw c_uint8*? target_of(u8[] data) {
    if (len(data) == 0usize) { return null; }
    unsafe {
        raw u8* first = &data[0usize] as raw u8*;
        raw void* erased = first as raw void*;
        return erased as raw c_uint8*;
    }
}

/* ---- Errors ---- */

/* R-SLIB-SQLITE-0002: why an operation of std.sqlite failed. */
@derive(format)
enum error_code {
    sql,
    busy,
    locked,
    read_only,
    cannot_open,
    corrupt,
    full,
    io,
    constraint,
    too_big,
    mismatch,
    range,
    misuse,
    interrupted,
    other,
    invalid_text,
    no_statement,
    several_statements,
    parameter_count,
    column_range,
    type_mismatch,
    null_value,
};

/* R-SLIB-SQLITE-0002: a failure. native_code is the extended result code of SQLite, zero when
   the module refused the operation itself, and message the message of SQLite or of the module. */
error sqlite_error { error_code code; i32 native_code; std.string::string message; };

/* The error of a primary result code of SQLite (the low byte of an extended one). */
protected error_code code_of(i32 native) {
    switch (native & 255i32) {
    case 1: return error_code::sql;
    case 5: return error_code::busy;
    case 6: return error_code::locked;
    case 8: return error_code::read_only;
    case 9: return error_code::interrupted;
    case 10: return error_code::io;
    case 11: return error_code::corrupt;
    case 13: return error_code::full;
    case 14: return error_code::cannot_open;
    case 18: return error_code::too_big;
    case 19: return error_code::constraint;
    case 20: return error_code::mismatch;
    case 21: return error_code::misuse;
    case 25: return error_code::range;
    case 26: return error_code::corrupt;
    default: return error_code::other;
    }
}

/* A refusal of the module with its own message. */
protected sqlite_error refusal(error_code code, str message) throws std.alloc::alloc_error {
    return sqlite_error {.code = code, .native_code = 0i32, .message = std.string::from_str(message)};
}

/* The text of a message that the provider copied; bytes that are not UTF-8 are dropped. */
protected std.string::string message_text(const u8[] data) throws std.alloc::alloc_error {
    try {
        return std.string::from_utf8(data);
    } catch (std.string::string_error rejected) {
        rejected as void;
        return std.string::create();
    }
}

/* The failure that a status of SQLite reports, with the message kept by a handle. A failure of
   memory is reported as std.alloc::alloc_error. */
protected sqlite_error failure_of(i32 status, const u8[] message) throws std.alloc::alloc_error {
    throw (status == 7i32) std.alloc::alloc_error::out_of_memory;
    if (status == EMBEDDED_ZERO) {
        return refusal(error_code::invalid_text, "the text contains a zero byte");
    }
    return sqlite_error {.code = code_of(status), .native_code = status, .message = message_text(message)};
}

/* ---- Calls of the provider ---- */

/* A handle that the provider made, the status of the call and, for a statement, the bytes of
   SQL text that it took. */
struct native_result { raw void*? handle; i32 status; usize consumed; };

/* The status of a step and, at the end of a statement, what it changed. */
struct step_result { i32 status; u64 changes; i64 last_row_id; };

protected bool is_null_handle(raw void*? handle) {
    unsafe { return handle == null; }
}

protected native_result native_open(const u8[] path, c_int32 flags, c_int32 busy_timeout) {
    raw void*? handle = null;
    unsafe {
        raw (raw void*?)* handle_out = &handle as raw (raw void*?)*;
        c_int32 status =
            r_std_sqlite_native_open(bytes_of(path), len(path) as c_size, flags, busy_timeout, handle_out);
        return native_result {.handle = handle, .status = status as i32, .consumed = 0usize};
    }
}

protected native_result native_prepare(raw void*? connection, const u8[] sql) {
    raw void*? statement = null;
    c_size used = 0usize as c_size;
    unsafe {
        raw (raw void*?)* statement_out = &statement as raw (raw void*?)*;
        raw c_size* used_out = &used as raw c_size*;
        c_int32 status =
            r_std_sqlite_native_prepare(connection, bytes_of(sql), len(sql) as c_size, statement_out, used_out);
        return native_result {.handle = statement, .status = status as i32, .consumed = used as usize};
    }
}

protected step_result native_step(raw void*? statement) {
    c_int64 changes = 0i64 as c_int64;
    c_int64 last_row_id = 0i64 as c_int64;
    unsafe {
        raw c_int64* changes_out = &changes as raw c_int64*;
        raw c_int64* last_out = &last_row_id as raw c_int64*;
        c_int32 status = r_std_sqlite_native_step(statement, changes_out, last_out);
        return step_result {.status = status as i32, .changes = changes as u64, .last_row_id = last_row_id as i64};
    }
}

protected i32 native_execute(raw void*? connection, const u8[] sql) {
    unsafe { return r_std_sqlite_native_execute(connection, bytes_of(sql), len(sql) as c_size) as i32; }
}

protected void native_finalize(raw void*? statement) {
    unsafe { r_std_sqlite_native_finalize(statement); }
}

protected void native_reset(raw void*? statement) {
    unsafe { r_std_sqlite_native_reset(statement); }
}

protected bool native_autocommit(raw void*? connection) {
    unsafe { return r_std_sqlite_native_autocommit(connection) != (0i32 as c_int32); }
}

protected usize native_parameter_count(raw void*? statement) {
    unsafe { return r_std_sqlite_native_parameter_count(statement) as usize; }
}

protected i32 native_column_count(raw void*? statement) {
    unsafe { return r_std_sqlite_native_column_count(statement) as i32; }
}

protected i32 native_column_type(raw void*? statement, i32 column) {
    unsafe { return r_std_sqlite_native_column_type(statement, column as c_int32) as i32; }
}

protected i64 native_column_integer(raw void*? statement, i32 column) {
    unsafe { return r_std_sqlite_native_column_integer(statement, column as c_int32) as i64; }
}

protected f64 native_column_real(raw void*? statement, i32 column) {
    unsafe { return r_std_sqlite_native_column_real(statement, column as c_int32) as f64; }
}

/* The provider copies at most len(target) bytes and returns the whole length; an empty target
   asks for the length alone. */
protected usize native_database_message(raw void*? connection, u8[] target) {
    unsafe {
        return r_std_sqlite_native_database_message(connection, target_of(target), len(target) as c_size)
            as usize;
    }
}

protected usize native_statement_message(raw void*? statement, u8[] target) {
    unsafe {
        return r_std_sqlite_native_statement_message(statement, target_of(target), len(target) as c_size)
            as usize;
    }
}

protected usize native_column_bytes(raw void*? statement, i32 column, u8[] target) {
    unsafe {
        return r_std_sqlite_native_column_bytes(statement, column as c_int32, target_of(target),
                                                len(target) as c_size) as usize;
    }
}

protected usize native_column_name(raw void*? statement, i32 column, u8[] target) {
    unsafe {
        return r_std_sqlite_native_column_name(statement, column as c_int32, target_of(target),
                                               len(target) as c_size) as usize;
    }
}

protected i32 native_bind(raw void*? statement, i32 index, const value* item) {
    c_int32 position = index as c_int32;
    unsafe {
        switch (*item) {
        case variant value::null_value: return r_std_sqlite_native_bind_null(statement, position) as i32;
        case variant value::integer(number):
            return r_std_sqlite_native_bind_integer(statement, position, *number as c_int64) as i32;
        case variant value::real(number):
            return r_std_sqlite_native_bind_real(statement, position, *number as c_double) as i32;
        case variant value::text(data):
            const u8[] encoded = *data;
            return r_std_sqlite_native_bind_text(statement, position, bytes_of(encoded), len(encoded) as c_size)
                as i32;
        case variant value::blob(data):
            const u8[] stored = data->as_slice();
            return r_std_sqlite_native_bind_blob(statement, position, bytes_of(stored), len(stored) as c_size)
                as i32;
        }
    }
}

/* The message kept by a database handle. */
protected bytes database_message(raw void*? handle) throws std.alloc::alloc_error {
    bytes nothing = std.alloc::bytes(0usize, 0u8);
    bytes data = std.alloc::bytes(native_database_message(handle, nothing.as_slice_mut()), 0u8);
    native_database_message(handle, data.as_slice_mut()) as void;
    return move data;
}

/* The message kept by a statement handle. */
protected bytes statement_message(raw void*? handle) throws std.alloc::alloc_error {
    bytes nothing = std.alloc::bytes(0usize, 0u8);
    bytes data = std.alloc::bytes(native_statement_message(handle, nothing.as_slice_mut()), 0u8);
    native_statement_message(handle, data.as_slice_mut()) as void;
    return move data;
}

/* Throws the failure of a database handle unless status is zero. */
protected void check_database(raw void*? handle, i32 status) throws sqlite_error, std.alloc::alloc_error {
    if (status == 0i32) { return; }
    bytes message = database_message(handle);
    throw failure_of(status, message.as_slice());
}

/* Throws the failure of a statement handle unless status is zero. */
protected void check_statement(raw void*? handle, i32 status) throws sqlite_error, std.alloc::alloc_error {
    if (status == 0i32) { return; }
    bytes message = statement_message(handle);
    throw failure_of(status, message.as_slice());
}

/* ---- Values and rows ---- */

/* R-SLIB-SQLITE-0003: a value of SQLite in one of its five storage classes. */
enum value {
    null_value,
    integer(i64),
    real(f64),
    text(std.string::string),
    blob(bytes),
};

value value::of_text(str text) throws std.alloc::alloc_error {
    return value::text(std.string::from_str(text));
}

value value::of_blob(const u8[] data) throws std.alloc::alloc_error {
    bytes copied = std.bytes::with_capacity(len(data));
    std.bytes::append(&copied, data);
    return value::blob(move copied);
}

/* A truth value is stored as the integer 1 or 0. */
value value::of_bool(bool flag) {
    if (flag == true) { return value::integer(1i64); }
    return value::integer(0i64);
}

/* R-SLIB-SQLITE-0004: one row of a result, its values in the order of the columns. */
struct row { array<value> values; };

usize row::count(const row* this) {
    return len(this->values);
}

const value* row::at(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    if (column >= len(this->values)) {
        throw refusal(error_code::column_range, "the row has no such column");
    }
    return &this->values[column];
}

bool row::is_null(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    switch (*this->at(column)) {
    case variant value::null_value: return true;
    default: return false;
    }
}

protected sqlite_error wrong_type(const value* found) throws std.alloc::alloc_error {
    switch (*found) {
    case variant value::null_value: return refusal(error_code::null_value, "the column is NULL");
    default: return refusal(error_code::type_mismatch, "the column holds another storage class");
    }
}

i64 row::integer(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    const value* found = this->at(column);
    switch (*found) {
    case variant value::integer(number): return *number;
    default: break;
    }
    throw wrong_type(found);
}

/* An integer is read as the nearest real. */
f64 row::real(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    const value* found = this->at(column);
    switch (*found) {
    case variant value::real(number): return *number;
    case variant value::integer(number): return *number as f64;
    default: break;
    }
    throw wrong_type(found);
}

/* A truth value is the integer 0 or 1. */
bool row::boolean(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    const value* found = this->at(column);
    switch (*found) {
    case variant value::integer(number):
        if (*number == 0i64) { return false; }
        if (*number == 1i64) { return true; }
        break;
    default: break;
    }
    throw wrong_type(found);
}

str row::text(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    const value* found = this->at(column);
    switch (*found) {
    case variant value::text(data): return *data;
    default: break;
    }
    throw wrong_type(found);
}

const u8[] row::blob(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    const value* found = this->at(column);
    switch (*found) {
    case variant value::blob(data): return data->as_slice();
    default: break;
    }
    throw wrong_type(found);
}

o<i64> row::optional_integer(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    if (this->is_null(column) == true) { return o::none; }
    return o::some(this->integer(column));
}

o<f64> row::optional_real(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    if (this->is_null(column) == true) { return o::none; }
    return o::some(this->real(column));
}

o<str> row::optional_text(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    if (this->is_null(column) == true) { return o::none; }
    return o::some(this->text(column));
}

o<const u8[]> row::optional_blob(const row* this, usize column) throws sqlite_error, std.alloc::alloc_error {
    if (this->is_null(column) == true) { return o::none; }
    return o::some(this->blob(column));
}

/* R-SLIB-SQLITE-0005: the rows of a query with the names of its columns. */
struct rows { array<std.string::string> columns; array<row> items; };

protected bool same_text(str left, str right) {
    const u8[] first = left;
    const u8[] second = right;
    if (len(first) != len(second)) { return false; }
    for (usize index = 0usize; index < len(first); index += 1usize) {
        if (first[index] != second[index]) { return false; }
    }
    return true;
}

o<usize> rows::column(const rows* this, str name) {
    for (usize index = 0usize; index < len(this->columns); index += 1usize) {
        if (same_text(this->columns[index], name) == true) { return o::some(index); }
    }
    return o::none;
}

/* R-SLIB-SQLITE-0005: what a statement changed: the rows that it inserted, updated or deleted,
   and the row id of the last insertion of the connection. */
struct execution { u64 changes; i64 last_row_id; };

/* ---- Connections and statements ---- */

/* R-SLIB-SQLITE-0006: how open opens a database. */
struct options {
    bool read_only = false;
    bool create = true;
    bool wal = false;
    bool foreign_keys = true;
    u32 busy_timeout_ms = 5000u32;
};

/* R-SLIB-SQLITE-0008: how begin starts a transaction. */
enum begin_mode { deferred, immediate, exclusive };

/* The connection of a database, shared by its statements and closed with the last of them. */
struct connection { protected atomic raw void*? native; };

drop(connection* self) {
    raw void*? handle = core::atomic_load(&self->native, core::memory_order::relaxed);
    unsafe { r_std_sqlite_native_close(handle); }
}

protected raw void*? connection_handle(const connection* owner) {
    return core::atomic_load(&owner->native, core::memory_order::relaxed);
}

/* A prepared statement and the connection that it keeps open. A run holds running from its
   first reset to its last, so that runs of one statement from several pool threads take turns
   instead of interleaving their binds and steps (R-SLIB-SQLITE-0007). */
struct prepared {
    protected atomic raw void*? native;
    protected arc connection owner;
    protected std.sync::mutex<bool> running;
};

drop(prepared* self) {
    raw void*? handle = core::atomic_load(&self->native, core::memory_order::relaxed);
    unsafe { r_std_sqlite_native_finalize(handle); }
}

protected raw void*? statement_handle(const prepared* statement) {
    return core::atomic_load(&statement->native, core::memory_order::relaxed);
}

/* R-SLIB-SQLITE-0006: an open database. */
struct database { protected arc connection shared; };

/* R-SLIB-SQLITE-0007: a statement prepared on a database. */
struct statement { protected arc prepared core; };

/* ---- Work on the blocking call pool ---- */

protected void push_value(array<value>* target, value item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<value> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void push_row(array<row>* target, row item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<row> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void push_name(array<std.string::string>* target, std.string::string item)
    throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.string::string> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Runs the statements of sql on a connection. */
protected void execute_on(raw void*? handle, const u8[] sql) throws sqlite_error, std.alloc::alloc_error {
    check_database(handle, native_execute(handle, sql));
}

/* Prepares the one statement of sql on a connection; a text with no statement or with a second
   one is refused. */
protected prepared prepare_on(arc connection owner, const u8[] sql) throws sqlite_error, std.alloc::alloc_error {
    raw void*? connection = connection_handle(&*owner);
    native_result first = native_prepare(connection, sql);
    check_database(connection, first.status);
    if (is_null_handle(first.handle) == true) {
        throw refusal(error_code::no_statement, "the text holds no statement");
    }
    prepared made = prepared {.native = first.handle, .owner = move owner,
                              .running = std.sync::mutex_new(false)};
    if (first.consumed < len(sql)) {
        native_result next = native_prepare(connection, sql[first.consumed..len(sql)]);
        check_database(connection, next.status);
        if (is_null_handle(next.handle) == false) {
            native_finalize(next.handle);
            throw refusal(error_code::several_statements, "the text holds more than one statement");
        }
    }
    return move made;
}

/* Binds the values in the order of the parameters; their number shall match. */
protected void bind_all(raw void*? statement, const array<value>* parameters)
    throws sqlite_error, std.alloc::alloc_error {
    if (len(*parameters) != native_parameter_count(statement)) {
        throw refusal(error_code::parameter_count,
                      "the number of values differs from the parameters of the statement");
    }
    const value[] listed = std.array::as_slice(parameters);
    for (usize index = 0usize; index < len(listed); index += 1usize) {
        check_statement(statement, native_bind(statement, (index + 1usize) as i32, &listed[index]));
    }
}

/* The bytes of a text or blob column of the current row. */
protected bytes column_bytes(raw void*? statement, i32 column) throws std.alloc::alloc_error {
    bytes nothing = std.alloc::bytes(0usize, 0u8);
    bytes data = std.alloc::bytes(native_column_bytes(statement, column, nothing.as_slice_mut()), 0u8);
    native_column_bytes(statement, column, data.as_slice_mut()) as void;
    return move data;
}

/* Text of the database as a string; SQLite does not check that it is UTF-8. */
protected std.string::string text_of(const u8[] data) throws sqlite_error, std.alloc::alloc_error {
    try {
        return std.string::from_utf8(data);
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    throw refusal(error_code::invalid_text, "a text value is not UTF-8");
}

protected value column_value(raw void*? statement, i32 column) throws sqlite_error, std.alloc::alloc_error {
    i32 kind = native_column_type(statement, column);
    if (kind == 1i32) { return value::integer(native_column_integer(statement, column)); }
    if (kind == 2i32) { return value::real(native_column_real(statement, column)); }
    if (kind == 3i32) {
        /* The bytes become the string without a second copy once they are known to be UTF-8. */
        std.string::from_bytes_result checked = std.string::from_bytes(column_bytes(statement, column));
        switch (move checked) {
        case variant std.string::from_bytes_result::valid(move text): return value::text(move text);
        case variant std.string::from_bytes_result::invalid(move rejected):
            drop rejected;
            throw refusal(error_code::invalid_text, "a text value is not UTF-8");
        }
    }
    if (kind == 4i32) { return value::blob(column_bytes(statement, column)); }
    return value::null_value;
}

protected array<std.string::string> column_names(raw void*? statement) throws sqlite_error, std.alloc::alloc_error {
    array<std.string::string> names = [];
    for (i32 column = 0i32; column < native_column_count(statement); column += 1i32) {
        bytes nothing = std.alloc::bytes(0usize, 0u8);
        bytes data = std.alloc::bytes(native_column_name(statement, column, nothing.as_slice_mut()), 0u8);
        native_column_name(statement, column, data.as_slice_mut()) as void;
        push_name(&names, text_of(data.as_slice()));
    }
    return move names;
}

/* Binds the parameters and steps the statement to its end; with collect, the rows are kept. */
protected execution run_steps(raw void*? statement, const array<value>* parameters, rows* out, bool collect)
    throws sqlite_error, std.alloc::alloc_error {
    bind_all(statement, parameters);
    if (collect == true) { out->columns = column_names(statement); }
    while (true) {
        step_result step = native_step(statement);
        if (step.status == 101i32) {
            return execution {.changes = step.changes, .last_row_id = step.last_row_id};
        }
        if (step.status != 100i32) {
            check_statement(statement, step.status);
            throw refusal(error_code::misuse, "a step ended without a row");
        }
        if (collect == true) {
            array<value> values = [];
            for (i32 column = 0i32; column < native_column_count(statement); column += 1i32) {
                push_value(&values, column_value(statement, column));
            }
            push_row(&out->items, row {.values = move values});
        }
    }
}

/* Runs a prepared statement and leaves it reset, also after a failure, so that it holds no lock
   of the database between runs. */
protected execution run_reset(const prepared* target, const array<value>* parameters, rows* out, bool collect)
    throws sqlite_error, std.alloc::alloc_error {
    raw void*? statement = statement_handle(target);
    native_reset(statement);
    try {
        return run_steps(statement, parameters, out, collect);
    } finally {
        native_reset(statement);
    }
}

/* A run of a statement, after the runs of it that other threads started first. */
protected execution run_prepared(const prepared* target, const array<value>* parameters, rows* out, bool collect)
    throws sqlite_error, std.alloc::alloc_error {
    std.sync::lock_result<bool> locked = std.sync::lock(&target->running);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        execution done = run_reset(target, parameters, out, collect);
        drop guard;
        return done;
    case variant std.sync::lock_result::poisoned(move guard):
        execution done = run_reset(target, parameters, out, collect);
        drop guard;
        return done;
    case variant std.sync::lock_result::would_deadlock: break;
    }
    throw refusal(error_code::misuse, "a statement ran within its own run");
}

protected rows empty_rows() {
    array<std.string::string> columns = [];
    array<row> items = [];
    return rows {.columns = move columns, .items = move items};
}

/* The busy timeout as the provider takes it. */
protected c_int32 timeout_of(u32 milliseconds) {
    if (milliseconds > 2147483647u32) { return 2147483647i32 as c_int32; }
    return milliseconds as c_int32;
}

/* Switches a connection to write-ahead logging; an in-memory database keeps its own mode. */
protected void use_wal(arc connection owner) throws sqlite_error, std.alloc::alloc_error {
    prepared mode = prepare_on(move owner, "PRAGMA journal_mode = WAL");
    array<value> none = [];
    rows result = empty_rows();
    run_prepared(&mode, &none, &result, true) as void;
    str chosen = result.items[0usize].text(0usize);
    if (same_text(chosen, "wal") == false && same_text(chosen, "memory") == false) {
        throw refusal(error_code::other, "the database cannot use write-ahead logging");
    }
}

protected database open_entry(std.string::string path, options settings) throws sqlite_error, std.alloc::alloc_error {
    c_int32 flags = 0i32 as c_int32;
    if (settings.read_only == true) { flags = 1i32 as c_int32; }
    if (settings.read_only == false && settings.create == true) { flags = 2i32 as c_int32; }
    native_result opened = native_open(path, flags, timeout_of(settings.busy_timeout_ms));
    throw (is_null_handle(opened.handle) == true) std.alloc::alloc_error::out_of_memory;
    connection owned = connection {.native = opened.handle};
    check_database(opened.handle, opened.status);
    arc connection shared = new arc connection(move owned);
    if (settings.foreign_keys == true) {
        execute_on(connection_handle(&*shared), "PRAGMA foreign_keys = ON");
    }
    if (settings.wal == true && settings.read_only == false) {
        use_wal(std.arc::clone(&shared));
    }
    return database {.shared = move shared};
}

protected rows query_entry(arc prepared target, array<value> parameters) throws sqlite_error, std.alloc::alloc_error {
    rows result = empty_rows();
    run_prepared(&*target, &parameters, &result, true) as void;
    return move result;
}

protected execution execute_entry(arc prepared target, array<value> parameters)
    throws sqlite_error, std.alloc::alloc_error {
    rows unused = empty_rows();
    return run_prepared(&*target, &parameters, &unused, false);
}

protected statement prepare_entry(arc connection owner, std.string::string sql)
    throws sqlite_error, std.alloc::alloc_error {
    prepared made = prepare_on(move owner, sql);
    return statement {.core = new arc prepared(move made)};
}

protected rows query_once_entry(arc connection owner, std.string::string sql, array<value> parameters)
    throws sqlite_error, std.alloc::alloc_error {
    prepared made = prepare_on(move owner, sql);
    rows result = empty_rows();
    run_prepared(&made, &parameters, &result, true) as void;
    return move result;
}

protected execution execute_once_entry(arc connection owner, std.string::string sql, array<value> parameters)
    throws sqlite_error, std.alloc::alloc_error {
    prepared made = prepare_on(move owner, sql);
    rows unused = empty_rows();
    return run_prepared(&made, &parameters, &unused, false);
}

protected void script_entry(arc connection owner, std.string::string sql) throws sqlite_error, std.alloc::alloc_error {
    execute_on(connection_handle(&*owner), sql);
}

protected void release_entry(database closing) {
    drop closing;
}

/* ---- The asynchronous interface ---- */

/* Every operation below copies its text, keeps the connection or the statement through a new
   reference and returns the call that it started on the blocking call pool, so its task borrows
   nothing from the caller (Core R-BORROW-0024). */

/* R-SLIB-SQLITE-0006: opens the database file at path, or a private in-memory database for
   ":memory:". */
task<database throws sqlite_error, std.alloc::alloc_error> open(str path, options settings)
    throws std.async::start_error, std.alloc::alloc_error {
    std.string::string copied = std.string::from_str(path);
    return std.async::blocking(open_entry, move copied, settings);
}

/* R-SLIB-SQLITE-0007: prepares the one statement of sql. */
task<statement throws sqlite_error, std.alloc::alloc_error> database::prepare(const database* this, str sql)
    throws std.async::start_error, std.alloc::alloc_error {
    std.string::string copied = std.string::from_str(sql);
    arc connection owner = std.arc::clone(&this->shared);
    return std.async::blocking(prepare_entry, move owner, move copied);
}

/* R-SLIB-SQLITE-0007: prepares, runs and finalizes the one statement of sql. */
task<rows throws sqlite_error, std.alloc::alloc_error> database::query(const database* this, str sql,
                                                                     array<value> parameters)
    throws std.async::start_error, std.alloc::alloc_error {
    std.string::string copied = std.string::from_str(sql);
    arc connection owner = std.arc::clone(&this->shared);
    return std.async::blocking(query_once_entry, move owner, move copied, move parameters);
}

task<execution throws sqlite_error, std.alloc::alloc_error> database::execute(const database* this, str sql,
                                                                            array<value> parameters)
    throws std.async::start_error, std.alloc::alloc_error {
    std.string::string copied = std.string::from_str(sql);
    arc connection owner = std.arc::clone(&this->shared);
    return std.async::blocking(execute_once_entry, move owner, move copied, move parameters);
}

/* R-SLIB-SQLITE-0007: runs every statement of sql in order, without parameters. */
task<void throws sqlite_error, std.alloc::alloc_error> database::execute_script(const database* this, str sql)
    throws std.async::start_error, std.alloc::alloc_error {
    std.string::string copied = std.string::from_str(sql);
    arc connection owner = std.arc::clone(&this->shared);
    return std.async::blocking(script_entry, move owner, move copied);
}

/* R-SLIB-SQLITE-0008: transactions of the connection. */
task<void throws sqlite_error, std.alloc::alloc_error> database::begin(const database* this, begin_mode mode)
    throws std.async::start_error, std.alloc::alloc_error {
    str text = "BEGIN DEFERRED";
    if (mode == begin_mode::immediate) { text = "BEGIN IMMEDIATE"; }
    if (mode == begin_mode::exclusive) { text = "BEGIN EXCLUSIVE"; }
    return this->execute_script(text);
}

task<void throws sqlite_error, std.alloc::alloc_error> database::commit(const database* this)
    throws std.async::start_error, std.alloc::alloc_error {
    return this->execute_script("COMMIT");
}

task<void throws sqlite_error, std.alloc::alloc_error> database::rollback(const database* this)
    throws std.async::start_error, std.alloc::alloc_error {
    return this->execute_script("ROLLBACK");
}

bool database::in_transaction(const database* this) {
    return native_autocommit(connection_handle(&*this->shared)) == false;
}

/* R-SLIB-SQLITE-0006: closes the connection on the blocking call pool once its statements are
   gone; dropping a database closes it where it is dropped. */
task<void> database::close(database this) throws std.async::start_error {
    return std.async::blocking(release_entry, move this);
}

/* R-SLIB-SQLITE-0007: runs the statement with the values of its parameters. */
task<rows throws sqlite_error, std.alloc::alloc_error> statement::query(const statement* this,
                                                                      array<value> parameters)
    throws std.async::start_error {
    arc prepared target = std.arc::clone(&this->core);
    return std.async::blocking(query_entry, move target, move parameters);
}

task<execution throws sqlite_error, std.alloc::alloc_error> statement::execute(const statement* this,
                                                                             array<value> parameters)
    throws std.async::start_error {
    arc prepared target = std.arc::clone(&this->core);
    return std.async::blocking(execute_entry, move target, move parameters);
}
