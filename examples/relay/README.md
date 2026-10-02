# Buffered line protocols

Read lines, records and replies without handling partial reads: `std.stream` (Library
R-SLIB-STREAM-0001..0004) gives standard input and output, files, TCP connections, child pipes
and owners of stream interfaces one `Reader` and `Writer`, `std.bufio` (R-SLIB-BUFIO-0001..0003)
buffers them, and `std.console` (R-SLIB-CONSOLE-0001..0002) prints and asks.

```sh
ctest --test-dir build-debug -R 'example_relay' --output-on-failure
printf 'alpha\r\nbeta\n\ngamma' | build-debug/tests/codegen_example_relay number
build-debug/tests/codegen_example_relay echo ping 'hello world'
```

`relay number` numbers the lines of standard input. The reader takes the input in reads of up
to 256 bytes and returns each line without its line feed and a carriage return before it; the
writer gathers the numbered lines in 64 bytes and hands them to standard output when that is
full and once more at the end. `NumberedLines` holds the reader and implements
`core::AsyncIterator` (Core R-TYPE-0046): its `@scoped async next` reads one line and returns it
numbered, or none at the end of the input. An asynchronous `for` (Core R-STMT-0021) starts
`next` in the group `io` at each iteration and awaits it:

```r
impl core::AsyncIterator for NumberedLines {
    type Item = std.string::string;
    @scoped async o<std.string::string> next(NumberedLines* this) throws std.error::fault {
        task_scope(1) io {
            if (await this->input.read_line(&this->line) == false) { return o::none; }
        }
        ...
    }
};

task_scope(1) io {
    for (std.string::string numbered in &numbered_input) {
        task_scope(1) write { await output.write(numbered.as_bytes()); }
    }
    await output.write_str("----\n");
}
```

```text
   1  alpha
   2  beta
   3  
   4  gamma
----
4 lines, 47 bytes left for the final flush
```

The operations of a reader and a writer are `@scoped`: they run inside a task group, and the
text they write is declared outside the group that writes it. A line longer than the reader's
buffer throws `std.io::io_error` with `resource_exhausted`, which reaches the main error
boundary (exit 113).

`relay fields FILE` reads a record file: the four bytes `RLY1` with `read_exact`, then fields
ending with `;` with `read_until`. A file with another start prints `relay: not a record file`
on standard error and exits with 65; a file that ends inside the four bytes throws
`std.bits::read_error` with `unexpected_end` (exit 117).

`relay count [FILE]` counts the bytes and line feeds of a file or of standard input. Which one
is decided when the program runs, so the source is an owner of the `Reader` interface, and a
buffered reader reads it in chunks with `read_into`:

```r
async own dyn(std.stream::Reader)* source(array<std.string::string> arguments) ... {
    if (len(arguments) == 3usize) {
        ...
        return new std.fs::file(move file);
    }
    return new std.io::input(std.io::stdin());
}
```

`relay echo WORD...` sends each word as a line over a loopback TCP connection. The server
reads the accepted `std.net::tcp_connection` with a buffered reader and answers each line with
its length through the connection's `Writer`; the client owns its stream as
`own dyn(std.stream::Stream)*` and reads the replies with a buffered reader of that owner:

```text
echo: 4 ping
echo: 11 hello world
2 lines served
```

`relay pipe WORD...` talks to `/bin/cat` through its two pipes as one stream,
`std.stream::duplex<std.io::input, std.io::output>`. It reads back as many lines as it sent
and then drops the stream, which closes both pipes, so `cat` ends:

```text
cat: red
cat: green
cat exited with 0
```

`relay ask` prints a prompt with `std.console::print`, reads one line with
`std.console::read_line`, which never reads past the line feed, and answers with `println`; at
the end of the input it writes `relay: no name given` with `eprintln` and exits with 65.

Exit statuses: 0 success, 64 usage, 65 invalid or missing input; failures of the streams reach
the main error boundary (113 input and output, 114 network, 117 other).
