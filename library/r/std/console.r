module std.console;

/* R-SLIB-CONSOLE-0001: the longest line that read_line accepts, in bytes. */
const usize max_line = 65536usize;

/* Writes the whole text with one write to one stream of the process console. */
protected async void emit(std.io::output stream, std.string::string text) throws std.error::fault {
    bytes buffer = (move text).into_bytes();
    std.io::write_all_result result = await stream.write_all(move buffer);
    switch (move result) {
    case variant std.io::write_all_result::written(move returned):
        drop returned;
    case variant std.io::write_all_result::failed(move failure):
        throw failure.error;
    }
}

/* R-SLIB-CONSOLE-0001: writes text to standard output. */
async void print(std.string::string text) throws std.error::fault {
    await emit(std.io::stdout(), move text);
}

/* R-SLIB-CONSOLE-0001: writes text and a line feed to standard output. */
async void println(std.string::string text) throws std.error::fault {
    std.string::append_str(&text, "\n");
    await emit(std.io::stdout(), move text);
}

/* R-SLIB-CONSOLE-0001: writes text to standard error. */
async void eprint(std.string::string text) throws std.error::fault {
    await emit(std.io::stderr(), move text);
}

/* R-SLIB-CONSOLE-0001: writes text and a line feed to standard error. */
async void eprintln(std.string::string text) throws std.error::fault {
    std.string::append_str(&text, "\n");
    await emit(std.io::stderr(), move text);
}

/* R-SLIB-CONSOLE-0002: the next line of standard input without its line feed and one carriage
   return before it, or none at the end of the input. It reads one byte at a time and never a byte
   after the line feed, so other readers of standard input continue after the line. */
async o<std.string::string> read_line() throws std.error::fault {
    bytes line = std.alloc::bytes(0usize, 0u8);
    u8[1] byte = {0u8};
    bool ended = false;
    std.io::input input = std.io::stdin();
    while (true) {
        usize count = 0usize;
        task_scope(1) io { count += await std.io::read_into(&input, byte[0usize..1usize]); }
        if (count == 0usize) {
            ended = true;
            break;
        }
        if (byte[0] == 10u8) { break; }
        throw (len(line) == max_line)
            std.io::io_error {.code = std.io::error_code::resource_exhausted, .native_code = 0i64};
        line.append(byte[0usize..1usize]);
    }
    if (ended == true && len(line) == 0usize) { return o::none; }
    usize length = len(line);
    if (length != 0usize && line[length - 1usize] == 13u8) { line.pop() as void; }
    std.string::string text = std.string::create();
    std.string::append_utf8(&text, line.as_slice());
    return o::some(move text);
}
