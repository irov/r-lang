module example.assistant.memory;
import std.text;
import std.mcp;

/* One note that the assistant keeps. */
struct note { u64 id; std.string::string text; };

/* What the handlers of the server share: the notes, the next id and the notifier through which
   the server tells subscribed clients about changes. */
struct memory {
    std.sync::mutex<array<note>> notes;
    atomic u64 next;
    std.mcp::notifier changes;
};

/* The arguments and results of the tools; their JSON Schemas come from these types. */
struct remember_args { @json(description = "What to remember") std.string::string text; };
struct remembered { @json(description = "The id of the new note") u64 id; };
struct recall_args {
    @json(description = "Words to look for, in any case") std.string::string query;
    @json(optional, description = "The most notes to return") u32 limit;
};
struct found_note { u64 id; std.string::string text; };
struct recalled { array<found_note> notes; };
struct forget_args { @json(description = "The id of the note") u64 id; };
struct confirm_form { @json(description = "Whether to forget the note") bool confirm; };

protected void keep(array<note>* notes, note item) throws std.alloc::alloc_error {
    try {
        notes->push(move item);
    } catch (std.array::push_error<note> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected void keep_found(array<found_note>* found, u64 id, str text) throws std.alloc::alloc_error {
    try {
        found->push(found_note {.id = id, .text = std.string::from_str(text)});
    } catch (std.array::push_error<found_note> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* Runs one of the operations on the notes under their lock. */
protected u64 add_note(const memory* state, str text) throws std.alloc::alloc_error {
    u64 id = core::atomic_fetch_add(&state->next, 1u64, core::memory_order::relaxed) + 1u64;
    std.sync::lock_result<array<note>> locked = std.sync::lock(&state->notes);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        keep(std.sync::mutex_guard_mut(&guard), note {.id = id, .text = std.string::from_str(text)});
    case variant std.sync::lock_result::poisoned(move guard):
        keep(std.sync::mutex_guard_mut(&guard), note {.id = id, .text = std.string::from_str(text)});
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return id;
}

/* Whether the text contains the query, ignoring the case of ASCII letters. */
protected bool matches(str text, str query) {
    const u8[] haystack = text;
    const u8[] needle = query;
    if (len(needle) == 0usize) { return true; }
    if (len(needle) > len(haystack)) { return false; }
    for (usize at = 0usize; at + len(needle) <= len(haystack); at += 1usize) {
        bool same = true;
        for (usize index = 0usize; index < len(needle) && same == true; index += 1usize) {
            same = std.text::to_ascii_lower(haystack[at + index]) == std.text::to_ascii_lower(needle[index]);
        }
        if (same == true) { return true; }
    }
    return false;
}

protected recalled find_notes(const memory* state, str query, u32 limit) throws std.alloc::alloc_error {
    recalled result = {.notes = std.array::create::<found_note>()};
    std.sync::lock_result<array<note>> locked = std.sync::lock(&state->notes);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        const array<note>* notes = std.sync::mutex_guard_ref(&guard);
        for (usize index = 0usize; index < len(*notes); index += 1usize) {
            if (limit != 0u32 && (len(result.notes) as u32) >= limit) { break; }
            if (matches((*notes)[index].text.as_str(), query) == true) {
                keep_found(&result.notes, (*notes)[index].id, (*notes)[index].text.as_str());
            }
        }
    case variant std.sync::lock_result::poisoned(move guard): drop guard;
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return move result;
}

/* The text of the note with the id, none when there is none. */
protected o<std.string::string> note_text(const memory* state, u64 id) throws std.alloc::alloc_error {
    std.sync::lock_result<array<note>> locked = std.sync::lock(&state->notes);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        const array<note>* notes = std.sync::mutex_guard_ref(&guard);
        for (usize index = 0usize; index < len(*notes); index += 1usize) {
            if ((*notes)[index].id == id) { return o::some(std.string::from_str((*notes)[index].text.as_str())); }
        }
    case variant std.sync::lock_result::poisoned(move guard): drop guard;
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return o::none;
}

protected bool remove_note(const memory* state, u64 id) {
    std.sync::lock_result<array<note>> locked = std.sync::lock(&state->notes);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        array<note>* notes = std.sync::mutex_guard_mut(&guard);
        for (usize index = 0usize; index < len(*notes); index += 1usize) {
            if ((*notes)[index].id == id) {
                o<note> removed = notes->remove(index);
                drop removed;
                return true;
            }
        }
    case variant std.sync::lock_result::poisoned(move guard): drop guard;
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return false;
}

/* Every note as a resource. */
protected array<std.mcp::resource> note_resources(const memory* state) throws std.alloc::alloc_error {
    array<std.mcp::resource> result = std.array::create::<std.mcp::resource>();
    std.sync::lock_result<array<note>> locked = std.sync::lock(&state->notes);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        const array<note>* notes = std.sync::mutex_guard_ref(&guard);
        for (usize index = 0usize; index < len(*notes); index += 1usize) {
            u64 id = (*notes)[index].id;
            std.string::string uri = f"memory://notes/{id}";
            std.string::string name = f"note {id}";
            try {
                result.push(std.mcp::resource::create(uri.as_str(), name.as_str(), "text/plain"));
            } catch (std.array::push_error<std.mcp::resource> rejected) {
                (move rejected) as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
        }
    case variant std.sync::lock_result::poisoned(move guard): drop guard;
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return move result;
}

/* The tool result of a failure that the model reads. */
protected std.mcp::tool_outcome failed(str text) throws std.alloc::alloc_error {
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_error(text));
}

protected std.string::string arguments_of(const std.mcp::call* request) throws std.alloc::alloc_error {
    try {
        return request->arguments_text();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.string::from_str("{}");
}

async std.mcp::tool_outcome remember(arc memory state, std.mcp::call request) throws std.error::fault {
    std.string::string text = arguments_of(&request);
    try {
        remember_args args = std.json::unmarshal(text.as_bytes());
        u64 id = add_note(&*state, args.text.as_str());
        state->changes.resources_changed();
        state->changes.resource_updated("memory://notes");
        remembered result = {.id = id};
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_structured(&result));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return failed("remember needs a text");
}

async std.mcp::tool_outcome recall(arc memory state, std.mcp::call request) throws std.error::fault {
    std.string::string text = arguments_of(&request);
    try {
        recall_args args = std.json::unmarshal(text.as_bytes());
        recalled result = find_notes(&*state, args.query.as_str(), args.limit);
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_structured(&result));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return failed("recall needs a query");
}

protected o<u64> forget_id(str text) throws std.alloc::alloc_error {
    try {
        forget_args args = std.json::unmarshal(text);
        return o::some(args.id);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return o::none;
}

/* What the answer under "confirm" says: 0 when there is none for this round, 1 when the user
   confirmed, 2 when the user did not. */
protected u32 decision_of(const std.mcp::input* given, str expected) {
    if (std.bytes::equal(given->state_text(), expected) == false) { return 0u32; }
    switch (given->get("confirm")) {
    case variant o::some(answer):
        if ((*answer)->action != std.mcp::answer_action::accept) { return 2u32; }
        switch ((*answer)->field("confirm")) {
        case variant o::some(value):
            if (std.json::boolean(*value) == true) { return 1u32; }
        case variant o::none: break;
        }
        return 2u32;
    case variant o::none: break;
    }
    return 0u32;
}

/* The request to confirm forgetting a note, or a failure when there is no such note. */
protected std.mcp::tool_outcome ask_to_forget(const memory* state, u64 id, str expected) throws std.alloc::alloc_error {
    o<std.string::string> known = note_text(state, id);
    switch (move known) {
    case variant o::some(move text):
        str shown = text.as_str();
        std.string::string question = f"Forget note {id}: {shown}?";
        std.mcp::input_required asked = std.mcp::input_required::create();
        try {
            asked.ask_form("confirm", question.as_str(), std.json::schema::<confirm_form>());
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
        asked.set_state(expected);
        return std.mcp::tool_outcome::needs_input(move asked);
    case variant o::none: break;
    }
    return failed("there is no note of that id");
}

/* Forgets a note after the user confirms it through a form; the state of the first round names
   the note, so a retry for another note asks again. */
async std.mcp::tool_outcome forget(arc memory state, std.mcp::call request) throws std.error::fault {
    std.string::string text = arguments_of(&request);
    o<u64> wanted = forget_id(text.as_str());
    u64 id = 0u64;
    switch (wanted) {
    case variant o::some(value): id = *value;
    case variant o::none: return failed("forget needs the id of a note");
    }
    std.string::string expected = f"forget:{id}";
    u32 decision = decision_of(&request.input, expected.as_str());
    if (decision == 1u32) {
        bool removed = remove_note(&*state, id);
        removed as void;
        state->changes.resources_changed();
        state->changes.resource_updated("memory://notes");
        std.string::string done = f"forgot note {id}";
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text(done.as_str()));
    }
    if (decision == 2u32) {
        std.string::string kept = f"kept note {id}";
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text(kept.as_str()));
    }
    return ask_to_forget(&*state, id, expected.as_str());
}

/* Counts the words of the notes, reporting its progress note by note when the client asked for
   it. */
async std.mcp::tool_outcome count(arc memory state, std.mcp::call request) throws std.error::fault {
    recalled all = find_notes(&*state, "", 0u32);
    u64 words = 0u64;
    o<f64> total = o::some(len(all.notes) as f64);
    std.mcp::progress* reporter = &request.progress;
    for (usize index = 0usize; index < len(all.notes); index += 1usize) {
        for (str word in std.text::split(all.notes[index].text.as_str(), " ")) {
            const u8[] bytes = word;
            if (len(bytes) != 0usize) { words += 1u64; }
        }
        if (reporter->wanted() == true) {
            bool reported = reporter->report((index + 1usize) as f64, total, "counted a note");
            reported as void;
        }
    }
    std.string::string text = f"{words} words";
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text(text.as_str()));
}

/* memory://notes: every note as JSON. */
async std.mcp::read_outcome all_notes(arc memory state, std.mcp::read request) throws std.error::fault {
    recalled all = find_notes(&*state, "", 0u32);
    array<std.mcp::contents> items = std.array::create::<std.mcp::contents>();
    try {
        std.string::string text = std.json::marshal(&all);
        items.push(std.mcp::contents::of_text(request.uri.as_str(), "application/json", text.as_str()));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    } catch (std.array::push_error<std.mcp::contents> rejected) {
        (move rejected) as void;
    }
    return std.mcp::read_outcome::complete(move items);
}

/* memory://notes/{id}: the text of one note. */
async std.mcp::read_outcome one_note(arc memory state, std.mcp::read request) throws std.error::fault {
    u64 id = 0u64;
    switch (request.variable("id")) {
    case variant o::some(text):
        try {
            id = std.convert::parse_u64(*text, 10u32);
        } catch (std.convert::parse_error rejected) {
            rejected as void;
            return std.mcp::read_outcome::not_found;
        }
    case variant o::none: return std.mcp::read_outcome::not_found;
    }
    o<std.string::string> found = note_text(&*state, id);
    switch (move found) {
    case variant o::some(move text):
        array<std.mcp::contents> items = std.array::create::<std.mcp::contents>();
        try {
            items.push(std.mcp::contents::of_text(request.uri.as_str(), "text/plain", text.as_str()));
        } catch (std.array::push_error<std.mcp::contents> rejected) {
            (move rejected) as void;
        }
        return std.mcp::read_outcome::complete(move items);
    case variant o::none: break;
    }
    return std.mcp::read_outcome::not_found;
}

async array<std.mcp::resource> list_notes(arc memory state) throws std.error::fault {
    return note_resources(&*state);
}

/* reflect(topic): a message that asks the model about the notes on a topic. */
async std.mcp::prompt_outcome reflect(arc memory state, std.mcp::prompt_call request) throws std.error::fault {
    str topic = "";
    switch (request.argument("topic")) {
    case variant o::some(value): topic = *value;
    case variant o::none: break;
    }
    recalled found = find_notes(&*state, topic, 0u32);
    std.string::string text = f"What do I know about {topic}?";
    for (usize index = 0usize; index < len(found.notes); index += 1usize) {
        std.string::append_str(&text, " Note: ");
        std.string::append_str(&text, found.notes[index].text.as_str());
        std.string::append_str(&text, ".");
    }
    std.mcp::prompt_result result = std.mcp::prompt_result::create("Reflects on the notes about a topic");
    result.say(std.mcp::role::user, text.as_str());
    return std.mcp::prompt_outcome::complete(move result);
}

/* Completes the topic of reflect with the words of the notes that begin with what was typed. */
async std.mcp::completion topics(arc memory state, std.mcp::completion_request request) throws std.error::fault {
    std.mcp::completion found = std.mcp::completion::create();
    recalled all = find_notes(&*state, "", 0u32);
    for (usize index = 0usize; index < len(all.notes); index += 1usize) {
        for (str word in std.text::split(all.notes[index].text.as_str(), " ")) {
            const u8[] bytes = word;
            if (len(bytes) == 0usize || std.text::starts_with(word, request.value.as_str()) == false) { continue; }
            bool known = false;
            for (usize other = 0usize; other < len(found.values); other += 1usize) {
                if (std.bytes::equal(found.values[other].as_bytes(), word) == true) { known = true; }
            }
            if (known == false) {
                try {
                    found.values.push(std.string::from_str(word));
                } catch (std.array::push_error<std.string::string> rejected) {
                    (move rejected) as void;
                }
            }
        }
    }
    return move found;
}

/* R: the hints of a tool for the client: whether it only reads, whether it destroys, whether a
   repeated call changes nothing more, and whether it reaches beyond the notes. */
protected std.mcp::tool_annotations hints(bool reading, bool destroying, bool repeatable) {
    return std.mcp::tool_annotations {.title = std.string::create(), .read_only = o::some(reading),
                                      .destructive = o::some(destroying), .idempotent = o::some(repeatable),
                                      .open_world = o::some(false)};
}

/* R: the decision about a bearer token when the server runs protected (the HTTP half of demo):
   the demo token may do everything, the read token everything but calling tools. A real server
   verifies that an authorization server issued the token for its resource. */
std.mcp::access check_token(const memory* state, str token, str method) throws std.alloc::alloc_error {
    state as void;
    if (std.bytes::equal(token, "demo-token") == true) { return std.mcp::access::allowed; }
    if (std.bytes::equal(token, "read-token") == true) {
        if (std.bytes::equal(method, "tools/call") == true) { return std.mcp::access::insufficient; }
        return std.mcp::access::allowed;
    }
    return std.mcp::access::invalid;
}

/* R: the MCP server of the assistant. forget and count run as tasks for the clients that declare
   the Tasks extension while a runner runs (std.mcp::run_tasks); a task is kept ten minutes and
   its client polls it every 50 milliseconds. */
std.mcp::server<memory> build() throws std.mcp::mcp_error, std.json::error, std.alloc::alloc_error {
    std.mcp::notifier changes = std.mcp::notifier::create();
    arc memory state = new arc memory {.notes = std.sync::mutex_new(std.array::create::<note>()), .next = 0u64,
                                       .changes = changes.share()};
    std.mcp::options settings = {.ttl_ms = 1000u64, .page_size = 50usize, .list_changed = true, .subscribe = true,
                                 .task_ttl_ms = 600000u64, .task_poll_ms = 50u64};
    std.mcp::server<memory> host =
        std.mcp::server<memory>::create(std.mcp::implementation::create("assistant-memory", "0.1.0"), move state,
                                        move changes, settings);
    host.set_instructions("Keeps short notes for an assistant: remember, recall and forget them.");
    std.mcp::tool adding = std.mcp::tool::create("remember", "Keeps a note", std.json::schema::<remember_args>());
    adding.output_schema = o::some(std.json::schema::<remembered>());
    adding.annotations = o::some(hints(false, false, false));
    host.add_tool(move adding, remember);
    std.mcp::tool finding = std.mcp::tool::create("recall", "Finds the notes that contain the query",
                                                  std.json::schema::<recall_args>());
    finding.output_schema = o::some(std.json::schema::<recalled>());
    finding.annotations = o::some(hints(true, false, true));
    host.add_tool(move finding, recall);
    std.mcp::tool removing = std.mcp::tool::create("forget", "Forgets a note after the user confirms it",
                                                   std.json::schema::<forget_args>());
    removing.annotations = o::some(hints(false, true, true));
    host.add_tool(move removing, forget);
    std.mcp::tool counting = std.mcp::tool::create("count", "Counts the words of the notes", std.mcp::no_arguments());
    counting.annotations = o::some(hints(true, false, true));
    host.add_tool(move counting, count);
    host.run_as_task("forget", false);
    host.run_as_task("count", false);
    host.add_resource(std.mcp::resource::create("memory://notes", "notes", "application/json"), all_notes);
    host.add_template(std.mcp::resource_template::create("memory://notes/{id}", "note", "text/plain"), one_note);
    host.list_resources_with(list_notes);
    std.mcp::prompt reflecting = std.mcp::prompt::create("reflect", "Reflects on the notes about a topic");
    reflecting.argument("topic", "What to reflect on", true);
    host.add_prompt(move reflecting, reflect);
    host.complete_with(topics);
    return move host;
}
