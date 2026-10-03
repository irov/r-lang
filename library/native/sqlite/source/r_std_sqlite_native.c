#include "r_std_sqlite_native.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

#include <limits.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdalign.h>
#include <stddef.h>
#include <string.h>

typedef struct RStdSqliteNativeDatabase {
    sqlite3 *connection;
    size_t message_length;
    char message[R_STD_SQLITE_NATIVE_MESSAGE_BYTES];
} RStdSqliteNativeDatabase;

typedef struct RStdSqliteNativeStatement {
    sqlite3_stmt *statement;
    size_t message_length;
    char message[R_STD_SQLITE_NATIVE_MESSAGE_BYTES];
} RStdSqliteNativeStatement;

/* The objects of the provider come from the hosted allocator of the runtime, like every other
   allocation of the standard library; SQLite allocates its own state with the C library.
   Outside a hosted program, as in the tests of the provider, an allocator of its own serves. */
static RRuntimeAllocator r_std_sqlite_native_local_allocator;
static pthread_once_t r_std_sqlite_native_allocator_once = PTHREAD_ONCE_INIT;

static void r_std_sqlite_native_allocator_start(void) {
    r_runtime_allocator_initialize(&r_std_sqlite_native_local_allocator);
}

static RRuntimeAllocator *r_std_sqlite_native_allocator(void) {
    RRuntimeAllocator *hosted = r_runtime_hosted_allocator();
    if (hosted != NULL) {
        return hosted;
    }
    (void)pthread_once(&r_std_sqlite_native_allocator_once, r_std_sqlite_native_allocator_start);
    return &r_std_sqlite_native_local_allocator;
}

static void *r_std_sqlite_native_allocate(size_t size) {
    void *allocation = NULL;
    if (r_runtime_allocator_allocate(
            r_std_sqlite_native_allocator(), size, alignof(max_align_t), &allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    return allocation;
}

static void r_std_sqlite_native_free(void *allocation) {
    if (allocation != NULL) {
        r_runtime_allocator_deallocate(allocation, alignof(max_align_t));
    }
}

/* A zero-terminated copy of text that holds no zero byte, or null when memory is exhausted. */
static char *r_std_sqlite_native_terminated(const uint8_t *text, size_t length) {
    char *copy = NULL;
    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = r_std_sqlite_native_allocate(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0U) {
        (void)memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

static int r_std_sqlite_native_has_zero(const uint8_t *text, size_t length) {
    return (length != 0U) && (memchr(text, 0, length) != NULL);
}

/* Keeps a message, cut before a character that would not fit whole. */
static size_t r_std_sqlite_native_keep(char *target, const char *message) {
    size_t length = 0U;
    if (message == NULL) {
        return 0U;
    }
    length = strlen(message);
    if (length > R_STD_SQLITE_NATIVE_MESSAGE_BYTES) {
        length = R_STD_SQLITE_NATIVE_MESSAGE_BYTES;
        while ((length > 0U) && ((((unsigned char)message[length]) & 0xC0U) == 0x80U)) {
            --length;
        }
    }
    (void)memcpy(target, message, length);
    return length;
}

static void r_std_sqlite_native_note_database(RStdSqliteNativeDatabase *database) {
    database->message_length =
        r_std_sqlite_native_keep(database->message, sqlite3_errmsg(database->connection));
}

static void r_std_sqlite_native_note_statement(RStdSqliteNativeStatement *statement) {
    statement->message_length = r_std_sqlite_native_keep(
        statement->message, sqlite3_errmsg(sqlite3_db_handle(statement->statement)));
}

static size_t
r_std_sqlite_native_copy(const void *data, size_t length, uint8_t *target, size_t capacity) {
    if ((data != NULL) && (target != NULL) && (capacity != 0U)) {
        (void)memcpy(target, data, length < capacity ? length : capacity);
    }
    return length;
}

static sqlite3_mutex *
r_std_sqlite_native_statement_mutex(const RStdSqliteNativeStatement *statement) {
    return sqlite3_db_mutex(sqlite3_db_handle(statement->statement));
}

int32_t r_std_sqlite_native_open(
    const uint8_t *path, size_t path_length, int32_t flags, int32_t busy_timeout, void **database) {
    RStdSqliteNativeDatabase *opened = NULL;
    char *name = NULL;
    int mode = SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_EXRESCODE;
    int status = SQLITE_OK;

    *database = NULL;
    opened = r_std_sqlite_native_allocate(sizeof(*opened));
    if (opened == NULL) {
        return SQLITE_NOMEM;
    }
    opened->connection = NULL;
    opened->message_length = 0U;
    *database = opened;
    if (r_std_sqlite_native_has_zero(path, path_length)) {
        return R_STD_SQLITE_NATIVE_EMBEDDED_ZERO;
    }
    /* Without threads the serialized mode that this provider relies on is unavailable. */
    if (sqlite3_threadsafe() == 0) {
        opened->message_length =
            r_std_sqlite_native_keep(opened->message, "SQLite was built without threads");
        return SQLITE_MISUSE;
    }
    name = r_std_sqlite_native_terminated(path, path_length);
    if (name == NULL) {
        return SQLITE_NOMEM;
    }
    if ((flags & 1) != 0) {
        mode |= SQLITE_OPEN_READONLY;
    } else {
        mode |= SQLITE_OPEN_READWRITE;
        if ((flags & 2) != 0) {
            mode |= SQLITE_OPEN_CREATE;
        }
    }
    status = sqlite3_open_v2(name, &opened->connection, mode, NULL);
    r_std_sqlite_native_free(name);
    if (opened->connection == NULL) {
        return SQLITE_NOMEM;
    }
    if (status == SQLITE_OK) {
        int persist = 1;
        (void)sqlite3_extended_result_codes(opened->connection, 1);
        /* The write-ahead log (emptied) and its index stay after the last connection closes, as
           the SQLite of Apple does by default, so that a read-only connection can open the
           database later: without them it could not create the index (SQLITE_CANTOPEN). */
        (void)sqlite3_file_control(opened->connection, "main", SQLITE_FCNTL_PERSIST_WAL, &persist);
        status = sqlite3_busy_timeout(opened->connection, busy_timeout < 0 ? 0 : busy_timeout);
    }
    if (status != SQLITE_OK) {
        status = sqlite3_extended_errcode(opened->connection);
        r_std_sqlite_native_note_database(opened);
        (void)sqlite3_close_v2(opened->connection);
        opened->connection = NULL;
        return status == SQLITE_OK ? SQLITE_ERROR : status;
    }
    return SQLITE_OK;
}

void r_std_sqlite_native_close(void *database) {
    RStdSqliteNativeDatabase *closing = database;
    if (closing == NULL) {
        return;
    }
    if (closing->connection != NULL) {
        (void)sqlite3_close_v2(closing->connection);
    }
    r_std_sqlite_native_free(closing);
}

size_t r_std_sqlite_native_database_message(void *database, uint8_t *target, size_t capacity) {
    RStdSqliteNativeDatabase *source = database;
    sqlite3_mutex *mutex = NULL;
    size_t length = 0U;
    if (source->connection != NULL) {
        mutex = sqlite3_db_mutex(source->connection);
    }
    sqlite3_mutex_enter(mutex);
    length = r_std_sqlite_native_copy(source->message, source->message_length, target, capacity);
    sqlite3_mutex_leave(mutex);
    return length;
}

size_t r_std_sqlite_native_statement_message(void *statement, uint8_t *target, size_t capacity) {
    RStdSqliteNativeStatement *source = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(source);
    size_t length = 0U;
    sqlite3_mutex_enter(mutex);
    length = r_std_sqlite_native_copy(source->message, source->message_length, target, capacity);
    sqlite3_mutex_leave(mutex);
    return length;
}

int32_t r_std_sqlite_native_execute(void *database, const uint8_t *sql, size_t length) {
    RStdSqliteNativeDatabase *target = database;
    sqlite3_mutex *mutex = sqlite3_db_mutex(target->connection);
    char *script = NULL;
    char *failure = NULL;
    int status = SQLITE_OK;

    if (r_std_sqlite_native_has_zero(sql, length)) {
        return R_STD_SQLITE_NATIVE_EMBEDDED_ZERO;
    }
    script = r_std_sqlite_native_terminated(sql, length);
    if (script == NULL) {
        return SQLITE_NOMEM;
    }
    sqlite3_mutex_enter(mutex);
    status = sqlite3_exec(target->connection, script, NULL, NULL, &failure);
    if (status != SQLITE_OK) {
        status = sqlite3_extended_errcode(target->connection);
        if (failure != NULL) {
            target->message_length = r_std_sqlite_native_keep(target->message, failure);
        } else {
            r_std_sqlite_native_note_database(target);
        }
    }
    sqlite3_mutex_leave(mutex);
    sqlite3_free(failure);
    r_std_sqlite_native_free(script);
    return status;
}

int32_t r_std_sqlite_native_prepare(
    void *database, const uint8_t *sql, size_t length, void **statement, size_t *consumed) {
    RStdSqliteNativeDatabase *source = database;
    RStdSqliteNativeStatement *prepared = NULL;
    sqlite3_mutex *mutex = sqlite3_db_mutex(source->connection);
    sqlite3_stmt *handle = NULL;
    const char *tail = NULL;
    static const char empty[1] = {'\0'};
    const char *text = (length == 0U) ? empty : (const char *)sql;
    int status = SQLITE_OK;

    *statement = NULL;
    *consumed = 0U;
    if (r_std_sqlite_native_has_zero(sql, length)) {
        return R_STD_SQLITE_NATIVE_EMBEDDED_ZERO;
    }
    if (length > (size_t)INT_MAX) {
        return SQLITE_TOOBIG;
    }
    prepared = r_std_sqlite_native_allocate(sizeof(*prepared));
    if (prepared == NULL) {
        return SQLITE_NOMEM;
    }
    sqlite3_mutex_enter(mutex);
    status = sqlite3_prepare_v3(
        source->connection, text, (int)length, SQLITE_PREPARE_PERSISTENT, &handle, &tail);
    if (status != SQLITE_OK) {
        status = sqlite3_extended_errcode(source->connection);
        r_std_sqlite_native_note_database(source);
    }
    sqlite3_mutex_leave(mutex);
    if ((status != SQLITE_OK) || (handle == NULL)) {
        r_std_sqlite_native_free(prepared);
        if (status == SQLITE_OK) {
            *consumed = length;
        }
        return status;
    }
    prepared->statement = handle;
    prepared->message_length = 0U;
    *consumed = (tail == NULL) ? length : (size_t)(tail - text);
    *statement = prepared;
    return SQLITE_OK;
}

int32_t r_std_sqlite_native_autocommit(void *database) {
    RStdSqliteNativeDatabase *source = database;
    return sqlite3_get_autocommit(source->connection) != 0 ? 1 : 0;
}

void r_std_sqlite_native_finalize(void *statement) {
    RStdSqliteNativeStatement *finishing = statement;
    if (finishing == NULL) {
        return;
    }
    (void)sqlite3_finalize(finishing->statement);
    r_std_sqlite_native_free(finishing);
}

int32_t r_std_sqlite_native_parameter_count(void *statement) {
    RStdSqliteNativeStatement *source = statement;
    return sqlite3_bind_parameter_count(source->statement);
}

/* The result of a binding: its failure keeps the message of the connection. */
static int32_t r_std_sqlite_native_bound(RStdSqliteNativeStatement *target, int status) {
    if (status != SQLITE_OK) {
        status = sqlite3_extended_errcode(sqlite3_db_handle(target->statement));
        r_std_sqlite_native_note_statement(target);
    }
    return status;
}

int32_t r_std_sqlite_native_bind_null(void *statement, int32_t index) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(target);
    int32_t status = SQLITE_OK;
    sqlite3_mutex_enter(mutex);
    status = r_std_sqlite_native_bound(target, sqlite3_bind_null(target->statement, index));
    sqlite3_mutex_leave(mutex);
    return status;
}

int32_t r_std_sqlite_native_bind_integer(void *statement, int32_t index, int64_t value) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(target);
    int32_t status = SQLITE_OK;
    sqlite3_mutex_enter(mutex);
    status = r_std_sqlite_native_bound(
        target, sqlite3_bind_int64(target->statement, index, (sqlite3_int64)value));
    sqlite3_mutex_leave(mutex);
    return status;
}

int32_t r_std_sqlite_native_bind_real(void *statement, int32_t index, double value) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(target);
    int32_t status = SQLITE_OK;
    sqlite3_mutex_enter(mutex);
    status =
        r_std_sqlite_native_bound(target, sqlite3_bind_double(target->statement, index, value));
    sqlite3_mutex_leave(mutex);
    return status;
}

int32_t
r_std_sqlite_native_bind_text(void *statement, int32_t index, const uint8_t *data, size_t length) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(target);
    static const char empty[1] = {'\0'};
    const char *text = (length == 0U) ? empty : (const char *)data;
    int32_t status = SQLITE_OK;
    sqlite3_mutex_enter(mutex);
    status = r_std_sqlite_native_bound(
        target,
        sqlite3_bind_text64(
            target->statement, index, text, (sqlite3_uint64)length, SQLITE_TRANSIENT, SQLITE_UTF8));
    sqlite3_mutex_leave(mutex);
    return status;
}

int32_t
r_std_sqlite_native_bind_blob(void *statement, int32_t index, const uint8_t *data, size_t length) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(target);
    int32_t status = SQLITE_OK;
    sqlite3_mutex_enter(mutex);
    if (length == 0U) {
        /* A null address would bind NULL; an empty blob stays a blob. */
        status =
            r_std_sqlite_native_bound(target, sqlite3_bind_zeroblob(target->statement, index, 0));
    } else {
        status = r_std_sqlite_native_bound(
            target,
            sqlite3_bind_blob64(
                target->statement, index, data, (sqlite3_uint64)length, SQLITE_TRANSIENT));
    }
    sqlite3_mutex_leave(mutex);
    return status;
}

int32_t r_std_sqlite_native_step(void *statement, int64_t *changes, int64_t *last_row_id) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3 *connection = sqlite3_db_handle(target->statement);
    sqlite3_mutex *mutex = sqlite3_db_mutex(connection);
    int status = SQLITE_OK;
    sqlite3_mutex_enter(mutex);
    status = sqlite3_step(target->statement);
    if (status == SQLITE_DONE) {
        *changes = (int64_t)sqlite3_changes64(connection);
        *last_row_id = (int64_t)sqlite3_last_insert_rowid(connection);
    } else if (status != SQLITE_ROW) {
        status = sqlite3_extended_errcode(connection);
        r_std_sqlite_native_note_statement(target);
    }
    sqlite3_mutex_leave(mutex);
    return status;
}

void r_std_sqlite_native_reset(void *statement) {
    RStdSqliteNativeStatement *target = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(target);
    sqlite3_mutex_enter(mutex);
    /* The result repeats the failure of the last step, which that step already reported. */
    (void)sqlite3_reset(target->statement);
    (void)sqlite3_clear_bindings(target->statement);
    sqlite3_mutex_leave(mutex);
}

int32_t r_std_sqlite_native_column_count(void *statement) {
    RStdSqliteNativeStatement *source = statement;
    return sqlite3_column_count(source->statement);
}

int32_t r_std_sqlite_native_column_type(void *statement, int32_t column) {
    RStdSqliteNativeStatement *source = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(source);
    int32_t type = SQLITE_NULL;
    sqlite3_mutex_enter(mutex);
    type = sqlite3_column_type(source->statement, column);
    sqlite3_mutex_leave(mutex);
    return type;
}

int64_t r_std_sqlite_native_column_integer(void *statement, int32_t column) {
    RStdSqliteNativeStatement *source = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(source);
    int64_t value = 0;
    sqlite3_mutex_enter(mutex);
    value = (int64_t)sqlite3_column_int64(source->statement, column);
    sqlite3_mutex_leave(mutex);
    return value;
}

double r_std_sqlite_native_column_real(void *statement, int32_t column) {
    RStdSqliteNativeStatement *source = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(source);
    double value = 0.0;
    sqlite3_mutex_enter(mutex);
    value = sqlite3_column_double(source->statement, column);
    sqlite3_mutex_leave(mutex);
    return value;
}

size_t r_std_sqlite_native_column_bytes(void *statement,
                                        int32_t column,
                                        uint8_t *target,
                                        size_t capacity) {
    RStdSqliteNativeStatement *source = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(source);
    const void *data = NULL;
    size_t length = 0U;
    sqlite3_mutex_enter(mutex);
    /* The address first, then the length, as SQLite requires after a conversion. */
    if (sqlite3_column_type(source->statement, column) == SQLITE_BLOB) {
        data = sqlite3_column_blob(source->statement, column);
    } else {
        data = sqlite3_column_text(source->statement, column);
    }
    length = (size_t)sqlite3_column_bytes(source->statement, column);
    length = r_std_sqlite_native_copy(data, data == NULL ? 0U : length, target, capacity);
    sqlite3_mutex_leave(mutex);
    return length;
}

size_t
r_std_sqlite_native_column_name(void *statement, int32_t column, uint8_t *target, size_t capacity) {
    RStdSqliteNativeStatement *source = statement;
    sqlite3_mutex *mutex = r_std_sqlite_native_statement_mutex(source);
    const char *name = NULL;
    size_t length = 0U;
    sqlite3_mutex_enter(mutex);
    name = sqlite3_column_name(source->statement, column);
    if (name != NULL) {
        length = r_std_sqlite_native_copy(name, strlen(name), target, capacity);
    }
    sqlite3_mutex_leave(mutex);
    return length;
}
