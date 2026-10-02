module example.runner.operations;
import example.runner.pipes;
import example.runner.pipes::{Capture};
import example.calculator.common::{Usage};

std.string::string describe(std.process::exit_status status) throws std.alloc::alloc_error {
    constexpr str name = core::enum_name(status.kind);
    std.string::string output = f"kind={name} code={status.code} success={status.success}\n";
    return move output;
}

async std.string::string execute(std.process::command command, bool capture, bool stop)
    throws Usage, std.process::process_error, std.io::io_error, std.alloc::alloc_error,
           std.array::push_error<u8>, std.string::string_error, std.async::start_error {
    std.process::spawn_result started = await (move command).spawn();
    switch (move started) {
    case variant std.process::spawn_result::failed(move failure):
        throw failure.error;
    case variant std.process::spawn_result::spawned(move child):
        u64 identity = child.id();
        std.string::string output = f"pid={identity}\n";
        if (capture == true) {
            o<std.io::output> input_pipe = child.take_stdin();
            await example.runner.pipes::close_stdin(move input_pipe);
            o<std.io::input> stdout_pipe = child.take_stdout();
            o<std.io::input> stderr_pipe = child.take_stderr();
            // Named tasks are intentional: these reads overlap before either is awaited.
            task<Capture> stdout_task = example.runner.pipes::collect(move stdout_pipe);
            task<Capture> stderr_task = example.runner.pipes::collect(move stderr_pipe);
            Capture stdout_text = await move stdout_task;
            Capture stderr_text = await move stderr_task;
            throw ((stdout_text.failure != 0) || (stderr_text.failure != 0))
                Usage { .message = "cannot capture child output (UTF-8, I/O or size limit)" };
            std.string::string captured = f"stdout:\n{stdout_text.text}\nstderr:\n{stderr_text.text}\n";
            str captured_view = captured.as_str();
            output.append(captured_view);
        }
        if (stop == true) { await child.terminate(); }
        std.process::wait_result finished = await (move child).wait();
        switch (move finished) {
        case variant std.process::wait_result::exited(move status):
            std.string::string details = describe(status);
            str view = details.as_str();
            output.append(view); break;
        case variant std.process::wait_result::failed(move failure):
            throw failure.error;
        }
        return move output;
    }
}
