/* The native provider of std.sqlite (Library R-SLIB-SQLITE-0001): a connection to an in-memory
   database, statements with every storage class, the failures and messages that the R part
   reports, and a connection closed before its last statement. */
#include "r_std_sqlite_native.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int r_sqlite_failures;

#define R_SQLITE_CHECK(condition)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            ++r_sqlite_failures;                                                                   \
        }                                                                                          \
    } while (0)

static const uint8_t *r_sqlite_text(const char *text) {
    return (const uint8_t *)text;
}

static void *r_sqlite_open_memory(void) {
    void *database = NULL;
    R_SQLITE_CHECK(r_std_sqlite_native_open(r_sqlite_text(":memory:"), 8U, 2, 1000, &database) ==
                   0);
    return database;
}

static void *r_sqlite_prepare(void *database, const char *sql) {
    void *statement = NULL;
    size_t consumed = 0U;
    R_SQLITE_CHECK(r_std_sqlite_native_prepare(
                       database, r_sqlite_text(sql), strlen(sql), &statement, &consumed) == 0);
    R_SQLITE_CHECK(consumed == strlen(sql));
    return statement;
}

static int r_sqlite_message_has(size_t (*read)(void *, uint8_t *, size_t),
                                void *handle,
                                const char *expected) {
    uint8_t message[R_STD_SQLITE_NATIVE_MESSAGE_BYTES + 1U];
    const size_t length = read(handle, message, R_STD_SQLITE_NATIVE_MESSAGE_BYTES);
    message[length] = 0U;
    return strstr((const char *)message, expected) != NULL;
}

static void r_sqlite_test_rows(void) {
    void *database = r_sqlite_open_memory();
    void *insert = NULL;
    void *select = NULL;
    int64_t changes = 0;
    int64_t row_id = 0;
    uint8_t buffer[16];
    const uint8_t blob[3] = {0U, 1U, 2U};

    R_SQLITE_CHECK(r_std_sqlite_native_autocommit(database) == 1);
    const char *schema =
        "CREATE TABLE item(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, score REAL, data "
        "BLOB);";
    R_SQLITE_CHECK(r_std_sqlite_native_execute(database, r_sqlite_text(schema), strlen(schema)) ==
                   0);
    insert = r_sqlite_prepare(database, "INSERT INTO item(name, score, data) VALUES (?1, ?2, ?3)");
    R_SQLITE_CHECK(r_std_sqlite_native_parameter_count(insert) == 3);
    R_SQLITE_CHECK(r_std_sqlite_native_bind_text(insert, 1, r_sqlite_text("käse"), 5U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_bind_real(insert, 2, 2.5) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_bind_blob(insert, 3, blob, sizeof(blob)) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_step(insert, &changes, &row_id) == 101);
    R_SQLITE_CHECK((changes == 1) && (row_id == 1));
    r_std_sqlite_native_reset(insert);
    /* Empty text and an empty blob stay empty values, not NULL. */
    R_SQLITE_CHECK(r_std_sqlite_native_bind_text(insert, 1, NULL, 0U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_bind_null(insert, 2) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_bind_blob(insert, 3, NULL, 0U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_step(insert, &changes, &row_id) == 101);
    R_SQLITE_CHECK(row_id == 2);
    r_std_sqlite_native_reset(insert);
    /* A repeated name fails with the extended code of a UNIQUE constraint and its message. */
    R_SQLITE_CHECK(r_std_sqlite_native_bind_text(insert, 1, r_sqlite_text("käse"), 5U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_step(insert, &changes, &row_id) == 2067);
    R_SQLITE_CHECK(r_sqlite_message_has(
        r_std_sqlite_native_statement_message, insert, "UNIQUE constraint failed: item.name"));
    r_std_sqlite_native_reset(insert);
    R_SQLITE_CHECK(r_std_sqlite_native_bind_integer(insert, 4, 1) == 25);

    select = r_sqlite_prepare(
        database, "SELECT id, name, score, data, typeof(name), typeof(data) FROM item ORDER BY id");
    R_SQLITE_CHECK(r_std_sqlite_native_column_count(select) == 6);
    R_SQLITE_CHECK(r_std_sqlite_native_column_name(select, 4, buffer, sizeof(buffer)) == 12U);
    R_SQLITE_CHECK(memcmp(buffer, "typeof(name)", 12U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_step(select, &changes, &row_id) == 100);
    R_SQLITE_CHECK(r_std_sqlite_native_column_type(select, 0) == 1);
    R_SQLITE_CHECK(r_std_sqlite_native_column_integer(select, 0) == 1);
    R_SQLITE_CHECK(r_std_sqlite_native_column_type(select, 1) == 3);
    R_SQLITE_CHECK(r_std_sqlite_native_column_bytes(select, 1, NULL, 0U) == 5U);
    R_SQLITE_CHECK(r_std_sqlite_native_column_bytes(select, 1, buffer, 2U) == 5U);
    R_SQLITE_CHECK(memcmp(buffer, "k\xc3", 2U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_column_type(select, 2) == 2);
    R_SQLITE_CHECK(r_std_sqlite_native_column_real(select, 2) == 2.5);
    R_SQLITE_CHECK(r_std_sqlite_native_column_type(select, 3) == 4);
    R_SQLITE_CHECK(r_std_sqlite_native_column_bytes(select, 3, buffer, sizeof(buffer)) == 3U);
    R_SQLITE_CHECK(memcmp(buffer, blob, 3U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_step(select, &changes, &row_id) == 100);
    R_SQLITE_CHECK(r_std_sqlite_native_column_type(select, 2) == 5);
    R_SQLITE_CHECK(r_std_sqlite_native_column_bytes(select, 1, buffer, sizeof(buffer)) == 0U);
    R_SQLITE_CHECK(r_std_sqlite_native_column_bytes(select, 4, buffer, sizeof(buffer)) == 4U);
    R_SQLITE_CHECK(memcmp(buffer, "text", 4U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_column_bytes(select, 5, buffer, sizeof(buffer)) == 4U);
    R_SQLITE_CHECK(memcmp(buffer, "blob", 4U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_step(select, &changes, &row_id) == 101);
    r_std_sqlite_native_finalize(select);
    r_std_sqlite_native_finalize(insert);
    r_std_sqlite_native_close(database);
}

static void r_sqlite_test_failures(void) {
    void *database = r_sqlite_open_memory();
    void *statement = NULL;
    size_t consumed = 0U;
    char long_name[700];
    size_t at = 0U;
    uint8_t message[R_STD_SQLITE_NATIVE_MESSAGE_BYTES];

    const char *wrong = "SELEC 1";
    R_SQLITE_CHECK(r_std_sqlite_native_prepare(
                       database, r_sqlite_text(wrong), strlen(wrong), &statement, &consumed) == 1);
    R_SQLITE_CHECK(statement == NULL);
    R_SQLITE_CHECK(
        r_sqlite_message_has(r_std_sqlite_native_database_message, database, "syntax error"));
    /* Only the first statement is prepared; the rest stays for the caller to refuse. */
    const char *two = "SELECT 1; SELECT 2";
    R_SQLITE_CHECK(r_std_sqlite_native_prepare(
                       database, r_sqlite_text(two), strlen(two), &statement, &consumed) == 0);
    R_SQLITE_CHECK((statement != NULL) && (consumed == 9U));
    r_std_sqlite_native_finalize(statement);
    statement = NULL;
    const char *comment = "  -- nothing";
    R_SQLITE_CHECK(r_std_sqlite_native_prepare(
                       database, r_sqlite_text(comment), strlen(comment), &statement, &consumed) ==
                   0);
    R_SQLITE_CHECK((statement == NULL) && (consumed == strlen(comment)));
    R_SQLITE_CHECK(r_std_sqlite_native_prepare(
                       database, r_sqlite_text("SELECT 1\0"), 9U, &statement, &consumed) ==
                   R_STD_SQLITE_NATIVE_EMBEDDED_ZERO);
    R_SQLITE_CHECK(r_std_sqlite_native_execute(database, r_sqlite_text("\0"), 1U) ==
                   R_STD_SQLITE_NATIVE_EMBEDDED_ZERO);
    /* A message longer than the handle keeps is cut before a character that does not fit. */
    (void)memcpy(long_name, "SELECT * FROM ", 14U);
    at = 14U;
    while (at + 2U < 614U) {
        long_name[at++] = '\xc3';
        long_name[at++] = '\xa9';
    }
    R_SQLITE_CHECK(r_std_sqlite_native_prepare(
                       database, r_sqlite_text(long_name), at, &statement, &consumed) == 1);
    R_SQLITE_CHECK(r_std_sqlite_native_database_message(database, message, sizeof(message)) ==
                   511U);
    R_SQLITE_CHECK(memcmp(message, "no such table: ", 15U) == 0);
    R_SQLITE_CHECK(message[510] == 0xA9U);
    /* Transactions are visible through the autocommit state. */
    R_SQLITE_CHECK(r_std_sqlite_native_execute(database, r_sqlite_text("BEGIN"), 5U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_autocommit(database) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_execute(database, r_sqlite_text("ROLLBACK"), 8U) == 0);
    R_SQLITE_CHECK(r_std_sqlite_native_autocommit(database) == 1);
    /* The connection lives until its last statement is finalized. */
    statement = r_sqlite_prepare(database, "SELECT 1");
    r_std_sqlite_native_close(database);
    r_std_sqlite_native_finalize(statement);
}

static void r_sqlite_test_open(void) {
    void *database = NULL;
    const char *missing = "/nonexistent-directory-of-r-tests/data.db";
    R_SQLITE_CHECK(
        r_std_sqlite_native_open(r_sqlite_text(missing), strlen(missing), 1, 0, &database) == 14);
    R_SQLITE_CHECK(database != NULL);
    R_SQLITE_CHECK(r_sqlite_message_has(
        r_std_sqlite_native_database_message, database, "unable to open database file"));
    r_std_sqlite_native_close(database);
    database = NULL;
    R_SQLITE_CHECK(r_std_sqlite_native_open(r_sqlite_text("a\0b"), 3U, 2, 0, &database) ==
                   R_STD_SQLITE_NATIVE_EMBEDDED_ZERO);
    r_std_sqlite_native_close(database);
}

/* The last connection to a database in write-ahead-log mode leaves the emptied log and its index,
   so that a read-only connection can open the database afterwards. */
static void r_sqlite_test_wal_files(void) {
    void *database = NULL;
    void *statement = NULL;
    int64_t changes = 0;
    int64_t row_id = 0;
    const char *name = "r_library_sqlite_native_wal.db";
    const char *setup =
        "PRAGMA journal_mode = WAL; CREATE TABLE IF NOT EXISTS t(x); INSERT INTO t VALUES (1);";
    R_SQLITE_CHECK(r_std_sqlite_native_open(r_sqlite_text(name), strlen(name), 2, 0, &database) ==
                   0);
    R_SQLITE_CHECK(r_std_sqlite_native_execute(database, r_sqlite_text(setup), strlen(setup)) == 0);
    r_std_sqlite_native_close(database);
    R_SQLITE_CHECK(access("r_library_sqlite_native_wal.db-wal", F_OK) == 0);
    R_SQLITE_CHECK(access("r_library_sqlite_native_wal.db-shm", F_OK) == 0);
    database = NULL;
    R_SQLITE_CHECK(r_std_sqlite_native_open(r_sqlite_text(name), strlen(name), 1, 0, &database) ==
                   0);
    statement = r_sqlite_prepare(database, "SELECT count(*) FROM t");
    R_SQLITE_CHECK(r_std_sqlite_native_step(statement, &changes, &row_id) == 100);
    R_SQLITE_CHECK(r_std_sqlite_native_column_integer(statement, 0) >= 1);
    r_std_sqlite_native_finalize(statement);
    r_std_sqlite_native_close(database);
    (void)unlink(name);
    (void)unlink("r_library_sqlite_native_wal.db-wal");
    (void)unlink("r_library_sqlite_native_wal.db-shm");
}

int main(void) {
    r_sqlite_test_rows();
    r_sqlite_test_failures();
    r_sqlite_test_open();
    r_sqlite_test_wal_files();
    if (r_sqlite_failures != 0) {
        (void)fprintf(stderr, "%d sqlite native checks failed\n", r_sqlite_failures);
        return 1;
    }
    (void)puts("r_library_sqlite_native_tests: ok");
    return 0;
}
