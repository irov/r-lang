# Byte streams over loaned buffers

Read standard input into one buffer, then pass the same bytes through a file, a loopback
TCP connection and a UDP datagram with the scoped I/O operations: `read_into`,
`write_from`, `write_all_from`, `tcp_read_into`, `tcp_write_from`, `tcp_write_all_from`,
`udp_send_from` and `udp_receive_into`. Every buffer stays owned by the caller; each
operation borrows a view of it as a loan of the enclosing `task_scope` that lasts until the
native backend acknowledges the outcome. No buffer moves into a task or comes back inside a
tagged result.

```sh
ctest --test-dir build-debug -R 'r_(frontend_codegen_example_streams|example_streams_commands)' --output-on-failure
printf 'scoped bytes' | build-debug/tests/codegen_example_streams /tmp/streams.bin 64
# stdin=12 crc32=...
# file=12 crc32=...
# tcp=12 crc32=...
# udp=12 truncated=false crc32=...
```

Each stage is a `@scoped async` helper that borrows the payload and a destination buffer;
`main` opens one group per stage so the loans end when the stage closes. Inside a stage, a
read loop reuses one buffer: the consuming `await` of each read observes its published
outcome and ends that read's loan, so the next read may borrow the window again. A
`write_from` call may make partial
progress and returns the count; `write_all_from` finishes the rest. The UDP destination
buffer is deliberately smaller than the payload once the input exceeds half the capacity,
so the datagram report shows `truncated=true` and the count of the bytes that were placed.

Exit statuses: 64 for usage errors, 65 for invalid arguments and 66 for filesystem errors
on FILE. Loopback network, I/O, allocation and task-start failures reach the implicit `main`
error boundary (114, 113, 112 and 116).
