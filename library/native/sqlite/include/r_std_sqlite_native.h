#ifndef R_STD_SQLITE_NATIVE_H
#define R_STD_SQLITE_NATIVE_H

/* The native provider of std.sqlite (Library R-SLIB-SQLITE-0001): the system SQLite library
   behind handles that the R part keeps. A database handle owns one connection in the serialized
   threading mode, so any thread may use it; every function below holds the mutex of that
   connection for its whole work, which keeps a failure and its message together. Functions that
   return int32_t report the extended result code of SQLite (0 for success, 100 for a row, 101
   for the end of a statement) or a negative status of the provider. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest message of a failure that a handle keeps, in bytes; longer messages are cut at a
   character boundary. */
#define R_STD_SQLITE_NATIVE_MESSAGE_BYTES 512U

/* The text of a path or a statement contains a zero byte. */
#define R_STD_SQLITE_NATIVE_EMBEDDED_ZERO (-2)

/* Opens the database at path (UTF-8, path_length bytes, not terminated). flags bit 0 opens it
   read only, bit 1 creates a missing file; busy_timeout is the number of milliseconds that a
   statement waits for a lock held by another connection. *database receives a handle also when
   opening fails, so that the message can be read, unless memory is exhausted (7 and null). */
int32_t r_std_sqlite_native_open(
    const uint8_t *path, size_t path_length, int32_t flags, int32_t busy_timeout, void **database);

/* Closes the connection of a database handle (null is ignored) and releases the handle. The
   connection itself lives until the last statement made from it is finalized. */
void r_std_sqlite_native_close(void *database);

/* The message of the last failure of a database or statement handle: copies at most capacity
   bytes to target and returns the length of the whole message. */
size_t r_std_sqlite_native_database_message(void *database, uint8_t *target, size_t capacity);
size_t r_std_sqlite_native_statement_message(void *statement, uint8_t *target, size_t capacity);

/* Runs every statement of sql (length bytes) in order, without parameters, discarding rows. */
int32_t r_std_sqlite_native_execute(void *database, const uint8_t *sql, size_t length);

/* Prepares the first statement of sql. *statement receives the handle, or null when sql holds
   no statement; *consumed receives the number of bytes of that statement. */
int32_t r_std_sqlite_native_prepare(
    void *database, const uint8_t *sql, size_t length, void **statement, size_t *consumed);

/* 1 when the connection is outside a transaction, 0 inside one. */
int32_t r_std_sqlite_native_autocommit(void *database);

/* Finalizes a statement handle (null is ignored) and releases it. */
void r_std_sqlite_native_finalize(void *statement);

/* The number of parameters of a statement, which is the largest index among them. */
int32_t r_std_sqlite_native_parameter_count(void *statement);

/* Binds parameter index (from 1). Text and blobs are copied; an empty one stays empty, not
   NULL. */
int32_t r_std_sqlite_native_bind_null(void *statement, int32_t index);
int32_t r_std_sqlite_native_bind_integer(void *statement, int32_t index, int64_t value);
int32_t r_std_sqlite_native_bind_real(void *statement, int32_t index, double value);
int32_t
r_std_sqlite_native_bind_text(void *statement, int32_t index, const uint8_t *data, size_t length);
int32_t
r_std_sqlite_native_bind_blob(void *statement, int32_t index, const uint8_t *data, size_t length);

/* Steps a statement: 100 with a row, 101 at the end, when *changes and *last_row_id receive
   the rows that it changed and the last inserted row id of the connection, or a failure. */
int32_t r_std_sqlite_native_step(void *statement, int64_t *changes, int64_t *last_row_id);

/* Resets a statement to its start and clears its parameters. */
void r_std_sqlite_native_reset(void *statement);

/* The columns of the current row: their number, the storage class of one (1 integer, 2 real,
   3 text, 4 blob, 5 NULL), its integer or real value, and the bytes of text or a blob, of
   which at most capacity are copied to target while the whole length is returned. */
int32_t r_std_sqlite_native_column_count(void *statement);
int32_t r_std_sqlite_native_column_type(void *statement, int32_t column);
int64_t r_std_sqlite_native_column_integer(void *statement, int32_t column);
double r_std_sqlite_native_column_real(void *statement, int32_t column);
size_t
r_std_sqlite_native_column_bytes(void *statement, int32_t column, uint8_t *target, size_t capacity);

/* The name of a result column, copied as the bytes of a column are. */
size_t
r_std_sqlite_native_column_name(void *statement, int32_t column, uint8_t *target, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
