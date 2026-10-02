# SQLite 3.53.4

The SQLite library that `std.sqlite` calls through the native provider of this directory, built
with the standard library instead of taken from the system, so
that every program sees the same version and the same SQL dialect on every macOS release
(Library R-SLIB-SQLITE-0001, R-SLIB-IDB-0017).

| | |
| --- | --- |
| Source | `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip` |
| SHA3-256 of the archive | `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` |
| Source id | `2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` |
| Files used | `sqlite3.c`, `sqlite3.h`, unchanged |
| License | public domain (see the header of `sqlite3.c`) |

The repository does not keep these files. Configuration downloads the archive once with
`r_third_party_archive` (`cmake/RThirdParty.cmake`) into `build/third_party`, which all build trees
share, checks its SHA3-256 and unpacks it there; an archive already there with that hash is used
without the network. The CMake target `r_sqlite3` in `CMakeLists.txt` compiles `sqlite3.c` with
these options:

| Option | Effect |
| --- | --- |
| `SQLITE_THREADSAFE=1` | serialized threading mode by default; the provider also opens every connection with `SQLITE_OPEN_FULLMUTEX` |
| `SQLITE_DQS=0` | a double-quoted name is always an identifier, never a string literal |
| `SQLITE_DEFAULT_MEMSTATUS=0` | no global memory statistics, no lock on every allocation |
| `SQLITE_OMIT_LOAD_EXTENSION=1` | no loadable extensions: a program cannot load code into SQLite |
| `SQLITE_OMIT_DEPRECATED=1`, `SQLITE_OMIT_SHARED_CACHE=1` | no deprecated interfaces, no shared-cache mode |
| `SQLITE_ENABLE_MATH_FUNCTIONS=1` | `sqrt`, `pow`, `ln`, `pi` and the other mathematical SQL functions |
| `SQLITE_ENABLE_FTS5=1` | full-text search tables (`USING fts5`) |
| `SQLITE_ENABLE_API_ARMOR=1` | misuse of the C interface returns `SQLITE_MISUSE` instead of undefined behavior |
| `HAVE_LOCALTIME_R=1` | the date functions use `localtime_r` |

JSON functions are part of every SQLite build since 3.38. On macOS the amalgamation itself selects
`pread`/`pwrite`, `nanosleep`, `F_FULLFSYNC` and the locking styles of Apple file systems.

To update: take the address and the SHA3-256 of the new amalgamation from
<https://www.sqlite.org/download.html>, change them in `CMakeLists.txt`, update this table, the
version in `library/r/links.json` and R-SLIB-IDB-0017 of the Library specification, and run the
`r_sqlite_vectors` differential test, which compares `std.sqlite` with the `sqlite3` module of
Python.
