# MCP server of notes for an assistant

A Model Context Protocol server (`std.mcp`, Library R-SLIB-MCP-0001..0024, revision 2026-07-28)
that keeps short notes for an assistant, and the MCP client of the same program talking to it. The
server offers four tools, a resource, a resource template, a prompt and completion, and runs two of
its tools as tasks of the Tasks extension. `serve` runs it on standard input and output, the way an
MCP host starts a local server, and `serve-http` runs it as a Streamable HTTP endpoint. `demo` has
the client of the same process hold one conversation over HTTP and then the same conversation over
stdio, with this program started as the server. The client discovers the server, calls the tools,
answers a confirmation form, listens for changes, reads resources, gets the prompt and follows
tasks. `replay` serves a recorded session from a file, and `frames` checks a stream of JSON-RPC
messages with `std.jsonrpc`.

```sh
ctest --test-dir build/debug -R 'example_assistant' --output-on-failure
build/debug/tests/codegen_example_assistant demo
build/debug/tests/codegen_example_assistant serve
build/debug/tests/codegen_example_assistant serve-http 8765
build/debug/tests/codegen_example_assistant replay session.jsonl
build/debug/tests/codegen_example_assistant schema recall
build/debug/tests/codegen_example_assistant inspect http://127.0.0.1:8765/mcp recall '{"query":"tea"}'
build/debug/tests/codegen_example_assistant inspect-stdio build/debug/tests/codegen_example_assistant serve
build/debug/tests/codegen_example_assistant frames < session.jsonl
```

`demo` prints the same conversation for both transports (only the HTTP half is shown here). The
HTTP server of the demo is protected, so its first two lines are refusals:

```text
http discover refused: unauthorized
http remember refused: forbidden
http discover assistant-memory 0.1.0 2026-07-28 tasks
http tools remember recall forget count
http remember {"id":1}
http remember {"id":2}
http listen resources memory://notes
http remember {"id":3}
http change resources
http change updated memory://notes
http recall {"notes":[{"id":1,"text":"tea with Ann on Friday"},{"id":2,"text":"green tea"}]}
  asks: Forget note 2: green tea?
http forget forgot note 2
http read memory://notes/1 tea with Ann on Friday
http read memory://notes/2 -32602 Resource not found
http resources memory://notes memory://notes/1 memory://notes/3
http prompt What do I know about tea? Note: tea with Ann on Friday.
http complete t -> tea the trip
http count 10 words
http task forget working
http task input_required Forget note 3: call Bob about the trip?
http listen tasks 1
http task completed forgot note 3
http task forget working
http task input_required Forget note 1: tea with Ann on Friday?
http task cancelled
http stopped
```

## The server

`memory.r` builds a `std.mcp::server<memory>`. `memory` is the state that the handlers share
through an `arc`: the notes under a mutex, the next id and a `std.mcp::notifier`, through which
handlers tell listening clients what changed. Every handler is an async function of the state and
its request:

```r
std.mcp::tool adding = std.mcp::tool::create("remember", "Keeps a note", std.json::schema::<remember_args>());
adding.output_schema = o::some(std.json::schema::<remembered>());
host.add_tool(move adding, remember);
```

The input and output schemas of the tools are not written by hand. `std.json::schema::<T>()`
(Core R-JSON-0001, Library R-SLIB-JSON-0002) builds a JSON Schema 2020-12 of a type from the same
model that `std.json::marshal` and `unmarshal` use. A field of the type is a property, and a field
without `@json(optional)` is required. An integer gets the bounds of its R type, and
`@json(description = ...)` sets the description:

```r
struct recall_args {
    @json(description = "Words to look for, in any case") std.string::string query;
    @json(optional, description = "The most notes to return") u32 limit;
};
```

`schema recall` prints that schema. A handler reads its arguments with `std.json::unmarshal` of
the same type. When they do not fit, it answers with a tool result that has `isError`, so the model
reads the reason; an unknown tool is a protocol error -32602.

- `remember` adds a note and returns `{"id": N}` as `structuredContent`. It also sends the same
  JSON as a text block for clients that do not read structured results.
- `recall` returns the notes that contain the query, ignoring case.
- `forget` removes a note only after the user confirms it (below).
- `count` counts the words of the notes. It reports its progress after each note, and the client
  receives the reports when its request carried a progress token.

Each tool also carries hints for the client (`std.mcp::tool_annotations`). `recall` and `count`
only read, `forget` destroys, and a repeated `recall`, `forget` or `count` changes nothing more.
None of the tools reaches beyond the notes.

`memory://notes` is a resource with every note as JSON. `memory://notes/{id}` is a template for a
single note, and a note that does not exist is `read_outcome::not_found`, which the server sends
as -32602 "Resource not found". `list_resources_with` adds a resource for every note to the list.
The prompt `reflect` takes a `topic` and returns one user message built from the notes on that
topic. Completion offers the words of the notes that start with what was typed.

## Asking the user: multi round-trip requests

In revision 2026-07-28 a server does not send requests of its own to the client. A handler that
needs the user returns `needs_input` with its questions. The request then ends with an
`InputRequiredResult`, and the client sends the same request again with the answers:

```r
std.mcp::input_required asked = std.mcp::input_required::create();
asked.ask_form("confirm", question.as_str(), std.json::schema::<confirm_form>());
asked.set_state(expected);
return std.mcp::tool_outcome::needs_input(move asked);
```

On the retry, `request.input` holds the answers under their keys, together with the state of the
first round. `forget` stores the id of the note in that state, so an answer given for another note
leads to a new question instead of a removal. A client that did not declare form elicitation gets
-32021 with the capability it lacks.

The client side is `call_tool_with` and a `std.mcp::elicitor`. The elicitor is a handler with its
own state that answers each form or URL request. `call_tool_with` repeats the request until the
result is complete. The demo's elicitor prints the question and accepts it.

## Tasks

The Tasks extension (`io.modelcontextprotocol/tasks`, R-SLIB-MCP-0021..0024) lets a server answer a
call of a tool with a task instead of a result. The client then polls the task, answers its
questions and reads its result later. `build` makes `forget` and `count` task tools:

```r
host.run_as_task("forget", false);
host.run_as_task("count", false);
```

The handlers stay as they are. The calls become tasks only for a client that declares the
extension, and only while `std.mcp::run_tasks` runs the tasks next to the transport. `serve`,
`serve-http` and the HTTP half of the demo start the runner in the same task group as the server
and stop it with a `std.service::stop` when the server ends. For other clients the tools run as
before; `true` instead of `false` would refuse those clients with -32021. A task is kept for
`task_ttl_ms` (ten minutes here) and suggests polling every `task_poll_ms` (50 milliseconds).

Inside a task, a handler that returns `needs_input` moves the task to `input_required`. The keys of
its questions become `1.confirm`, `2.confirm` and so on, one prefix per round, so a key never comes
back. When every question has its answer, the handler runs again with the answers in
`request.input` under its own keys. `progress.report` sets the status message of the task.

Both clients of the demo set `client_options.tasks`. For them `call_tool_with("forget", ...)` and
`call_tool("count", ...)` give the same lines as without tasks: the client follows the task to its
end, answering the question through the elicitor. The end of the demo uses the explicit API.
`start_tool` returns `tool_start::running` with the first `task_state` of the task. `get_task` polls
it until it asks for input, and `listen` with `interests.tasks` subscribes to its states.
`answer_task` sends the answer under the key `1.confirm`, and the subscription reports the
`change::state` of the completed task with its result. A second `forget` is cancelled with
`cancel_task` while it waits for the answer, and `finish_task` then fails with
`std.mcp::task_cancelled`.

## Changes and subscriptions

`remember` and `forget` call `resources_changed()` and `resource_updated("memory://notes")` on the
notifier. A client learns about changes through `subscriptions/listen`: `client::listen` opens the
stream with the kinds of change and the URIs it wants. It returns a `subscription` with what the
server granted, and `subscription::next` waits for the next `change`. In the demo the third note
arrives while the client listens, and the client sees `change resources` and then
`change updated memory://notes`. Over stdio the stream is the request `subscriptions/listen`,
which stays open until the client cancels it. Over HTTP it is an SSE response that keeps the
connection alive with comments. `unlisten` ends it, and the server closes the stream cleanly.

## Transports

`serve` is `std.mcp::serve_stdio`: one JSON-RPC message per line on standard input and output. The
server handles requests concurrently and writes each response as one line, and it stops when its
input ends; the tasks that remain then are cancelled. `serve-http` adds the endpoint to a `std.http::router` with `std.mcp::route` and serves
the router with `std.http::serve`:

```r
std.mcp::route(&routes, "/mcp");
std.service::report account = await std.http::serve(move listener, settings, move stop, move host,
                                                    move routes, bounds);
```

A POST carries one message. The server checks the `MCP-Protocol-Version`, `Mcp-Method` and
`Mcp-Name` headers against the body and answers a mismatch with 400 and -32020. It refuses an
`Origin` that is not local with 403 and accepts a notification with 202. It answers a request
with JSON, or with an SSE stream when progress or a subscription has to reach the client first.
A GET is 405, and closing the stream cancels the request.

The client is `std.mcp::client::over_http(url, ...)` or `std.mcp::launch(command, ...)`. `launch`
starts the server as a child process and connects its pipes. Over stdio, `client::run` is the task
that reads the server's messages and routes each one to the request it answers. `close` ends the
server's input, and the server then exits with status 0, which `demo` prints. The wait result
is only read, so `switch (finished)` borrows the `std.process::wait_result` place instead of
consuming it (Core R-STMT-0010), and its bindings are borrows of the payload:

```r
switch (finished) {
case variant std.process::wait_result::exited(exit): status = exit->code;
case variant std.process::wait_result::failed(failure): throw failure->error;
}
```

## Protecting the HTTP endpoint

A server over HTTP can be an OAuth 2.1 resource server (R-SLIB-MCP-0020). The demo protects its
server before serving it:

```r
std.mcp::protection guard = std.mcp::protection::create(address.as_str(), "https://auth.example");
guard.scope("notes.write");
built.protect(move guard, example.assistant.memory::check_token);
```

Every POST then needs `Authorization: Bearer TOKEN`, and `check_token` decides on each token
for the method of the message. A request without a valid token gets 401, and a token without
the scope that the method needs gets 403. Both answers carry a `WWW-Authenticate` challenge
that names the Protected Resource Metadata of RFC 9728, which `std.mcp::route` serves under
`/.well-known/oauth-protected-resource/mcp`. The client of the demo first has no token and is
refused with `std.mcp::unauthorized`. With `set_token("read-token")` its call of `remember`
is refused with `std.mcp::forbidden`, and with the demo token the conversation goes on. A real
server verifies that an authorization server issued the token for its resource.

## Replaying a session and checking frames

`replay FILE` serves the requests of a recorded session with `std.mcp::serve_streams`, which is
the stdio transport over any reader and writer; here the reader is the file and the writer is
standard output. The server runs the requests concurrently, so the responses come in the order
in which the requests finish, and `replay` returns once every request has finished.

`frames` reads JSON-RPC messages, one per line, from standard input with
`std.jsonrpc::read_line`. It writes each one in its canonical form to standard output with
`std.jsonrpc::write_message` and describes it on standard error. A line that differs from the
canonical form is marked `(rewritten)`. A line that is not a message is replaced by the error
response that a peer would send (`std.jsonrpc::error_response::of`):

```text
request 1 tools/list
request "a" x (rewritten)
invalid, answered with error - -32700 parse error: Parse error
error 8 -32602 invalid params: Unknown tool: x
```

## Inspecting a server

`inspect URL` and `inspect-stdio PROGRAM ARGUMENTS...` connect to any 2026-07-28 server. They print
what `server/discover` and the lists return and, with `TOOL JSON`, the result of one call. Errors
name their code: `mcp error -32602 (invalid params): Unknown tool: nothing`. The
behaviour test (`tests/run_assistant_examples.py`) also sends hand-written messages of the
revision to `serve` over a pipe and to `serve-http` over HTTP. It checks the schemas, the
multi-round confirmation, progress, the lists, the HTTP status codes and the errors. With the
Tasks extension it checks the tasks of `count` and `forget`, `tasks/get`, `tasks/update` and
`tasks/cancel`, `Mcp-Name` with the id of a task, and a subscription to tasks. It also replays
sessions and runs `frames` on valid and broken lines.

Independent implementations were also checked against this server by hand: the MCP Inspector
2.8.0 command line (`--protocol-era modern`), and the official Python SDK 2.2.0 as a client and as
a server for the R client, over both transports in both directions. The tasks were checked with
the official TypeScript client 2.2.0 and its Tasks package `@modelcontextprotocol/ext-tasks`
0.2.2, which ran `count` and `forget` as tasks over stdio and HTTP and answered the question of
`forget`. CTest does not depend on any of them; the results are recorded in sections 41 and 42 of
the [language completeness matrix](../../docs/language-completeness-matrix.ru.md).
