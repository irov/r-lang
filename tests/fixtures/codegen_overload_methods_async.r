module test.codegen.overload_methods_async;

error Finished { Done, };

async i32 echo(i32 value) { return value; }
async bytes echo(bytes value) { return move value; }

async i32 main() {
    i32 finalized = 0;
    try {
        i32 answer = await echo(42);
        if (answer != 42) { return 1; }
        bytes source = std.alloc::bytes(2usize, 7u8);
        bytes received = await echo(move source);
        const u8[] received_view = received.as_slice();
        if (len(received_view) != 2usize || received_view[0usize] != 7u8) { return 2; }
        std.string::string customer = std.string::from_str("Ada");
        std.format::builder builder = std.format::with_capacity(64usize);
        std.format::append(&builder, "Receipt for ");
        builder.append(customer);
        builder.append('\n');
        str view = builder;
        if (std.bytes::equal(view, "Receipt for Ada\n") == false) { return 3; }
        std.string::string receipt = (move builder).finish();
        usize receipt_size = receipt.len();
        if (receipt_size != 16usize) { return 4; }
        std.io::input input = std.io::stdin();
        try {
            // The wrapper injects start failure before the handle is transferred.
            await (move input).close(o::none);
            return 5;
        } catch (std.async::start_error failure) {
            await (move input).close(o::none);
        } finally { finalized += 1; }
        throw (finalized == 1) Finished::Done;
        return 6;
    } catch (Finished failure) { return 0; }
    catch (std.async::start_error failure) { return 10; }
    catch (std.alloc::alloc_error failure) { return 11; }
    catch (std.io::io_error failure) { return 12; }
}
