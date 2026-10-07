module example.relay.lines;
import std.stream;
import std.bufio;

/* The lines of a buffered reader with their numbers, as a core::AsyncIterator (Core R-TYPE-0046):
   each `next` reads one line and returns it numbered, or none at the end of the input. */
struct NumberedLines {
    std.bufio::reader<std.io::input> input;
    std.string::string line;
    u32 number;
};

impl core::AsyncIterator for NumberedLines {
    type Item = std.string::string;
    @scoped async o<std.string::string> next(NumberedLines* this) throws std.error::fault {
        task_scope(1) io {
            if (await this->input.read_line(&this->line) == false) { return o::none; }
        }
        this->number += 1u32;
        u32 number = this->number;
        str text = this->line;
        std.string::string numbered = f"{number:4}  {text}\n";
        return o::some(move numbered);
    }
};

/* Each line of standard input with its number. The buffered reader takes the input in reads of
   up to 256 bytes; the buffered writer gathers the numbered lines and hands them to standard
   output when 64 bytes are full and once more at the end. The asynchronous for (Core
   R-STMT-0021) starts `next` in the group `io` at each iteration. */
async u32 number_lines() throws std.error::fault {
    NumberedLines numbered_input = NumberedLines {
        .input = std.bufio::reader<std.io::input>::create(std.io::stdin(), 256usize),
        .line = std.string::create(),
        .number = 0u32,
    };
    std.bufio::writer<std.io::output> output =
        std.bufio::writer<std.io::output>::create(std.io::stdout(), 64usize);
    task_scope(1) io {
        for (std.string::string numbered in &numbered_input) {
            task_scope(1) write { await output.write(numbered); }
        }
        await output.write_str("----\n");
    }
    u32 number = numbered_input.number;
    usize pending = output.buffered();
    std.string::string total = f"{number} lines, {pending} bytes left for the final flush\n";
    task_scope(1) io {
        await output.write(total);
        await output.flush();
    }
    return number;
}

/* A record file: the four bytes "RLY1", then fields that end with ';', the last one possibly
   without. Returns the text to print, or none when the file does not start with the magic. */
async o<std.string::string> read_fields(std.fs::file file) throws std.error::fault {
    std.bufio::reader<std.fs::file> input = std.bufio::reader<std.fs::file>::create(move file, 64usize);
    bytes magic = std.alloc::bytes(4usize, 0u8);
    bytes field = std.alloc::bytes(0usize, 0u8);
    std.string::string text = std.string::create();
    std.string::reserve(&text, 64usize);
    bool valid = false;
    task_scope(1) io {
        bool whole = await input.read_exact(magic.as_slice_mut());
        if (whole == true && std.bytes::equal(magic.as_slice(), "RLY1") == true) { valid = true; }
        u32 index = 0u32;
        while (valid == true) {
            usize read = await input.read_until(59u8, &field);
            if (read == 0usize) { break; }
            index += 1u32;
            usize size = len(field);
            if (field[size - 1usize] == 59u8) { field.pop() as void; }
            std.string::string value = std.string::from_utf8(field.as_slice());
            std.string::string entry = f"field {index}: {value}\n";
            std.string::append_str(&text, entry);
            field.clear();
        }
    }
    drop field;
    if (valid == false) {
        drop text;
        return o::none;
    }
    return o::some(move text);
}

/* The bytes and lines of a source that the caller chose when the program ran: a file or
   standard input, owned through the Reader interface. */
async std.string::string measure(own dyn(std.stream::Reader)* source) throws std.error::fault {
    std.bufio::reader<own dyn(std.stream::Reader)*> input =
        std.bufio::reader<own dyn(std.stream::Reader)*>::create(move source, 16usize);
    bytes chunk = std.alloc::bytes(40usize, 0u8);
    u64 total = 0u64;
    u64 breaks = 0u64;
    task_scope(1) io {
        while (true) {
            usize read = await input.read_into(chunk.as_slice_mut());
            if (read == 0usize) { break; }
            total += read as u64;
            for (usize index = 0usize; index < read; index += 1usize) {
                if (chunk[index] == 10u8) { breaks += 1u64; }
            }
        }
    }
    return f"bytes={total} lines={breaks}\n";
}
