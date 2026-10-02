module test.codegen.scoped_console;

/* R-SLIB-IO-0008: read_into fills a loaned buffer until end of stream; write_from and
   write_all_from send a loaned view. The program echoes its standard input. */
async i32 main() {
    try {
        std.io::input input = std.io::stdin();
        std.io::output output = std.io::stdout();
        bytes buffer = std.alloc::bytes(256usize, 0u8);
        usize total = 0usize;
        task_scope(1) reader {
            while (total < 256usize) {
                u8[] window = buffer.as_slice_mut();
                usize count = await input.read_into(window[total..256usize], o::none);
                if (count == 0usize) { break; }
                total += count;
            }
        }
        await (move input).close(o::none);
        task_scope(1) writer {
            const u8[] head = buffer.as_slice();
            usize written = await output.write_from(head[0usize..total], o::none);
            const u8[] tail = buffer.as_slice();
            await std.io::write_all_from(&output, tail[written..total], o::none);
        }
        await output.flush(o::none);
        await (move output).close(o::none);
        return 0;
    } catch (std.io::io_error failure) { return 74; }
    catch (std.alloc::alloc_error failure) { return 71; }
    catch (std.async::start_error failure) { return 75; }
}
