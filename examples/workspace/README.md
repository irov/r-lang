# Workspace journal

A small file-management tool for journal records, metadata and publication. A record is
one version byte (`01`) followed by a UTF-8 message. Reading reports its length and CRC32;
`tail` computes the checksum of a bounded suffix without loading the prefix.

Build with `ctest --test-dir build-debug -R 'example_workspace' --output-on-failure`.
The binary is `build-debug/tests/codegen_example_workspace`.

```sh
workspace init /tmp/my-journal
workspace record /tmp/my-journal note 'First entry'
workspace stat /tmp/my-journal note
workspace read /tmp/my-journal note
workspace tail /tmp/my-journal/note 5
workspace publish /tmp/my-journal note published
workspace rename /tmp/my-journal note archived
workspace first /tmp/my-journal
workspace mkdir /tmp/my-journal drafts
workspace rmdir /tmp/my-journal drafts
workspace remove /tmp/my-journal archived
workspace inspect /tmp/my-journal/published
```

`record` and `publish` reject existing destinations. `rename` has the standard library's
replacement semantics. `remove` and `rmdir` delete the named entry; they do not recursively
remove directories. `init` creates exactly one directory and requires its parent to exist.
Use a disposable directory when following the commands above.

`inspect PATH` reads unrestricted metadata; `stat ROOT NAME` resolves relative to an open
directory. Reports include file kind, size and optional creation/modification/access times
as Unix seconds and nanoseconds. Missing timestamps print `unavailable`.

Relative operations use directory capabilities. Publication demonstrates the path-based
atomic no-replace operation: its destination must be a single filename under ROOT. `first`
reports one entry in native enumeration order, or `empty`; it is a presence/preview command,
not a recursive directory listing. File and directory handles are explicitly closed on
success and released by ordinary cleanup on errors.

Reads, publication input and tail output are limited to 1 MiB. The command tests run only in
Python temporary directories and independently verify file contents, CRC32, metadata,
no-replace behavior, missing files, relative path validation and empty directories.

Exit statuses: 0 success, 64 usage, 65 path/number conversion, 74 file error. I/O, allocation
and async-launch failures reach the implicit `main` error boundary (113, 112 and 116). Commands should be invoked by replacing `workspace` with the binary path.
