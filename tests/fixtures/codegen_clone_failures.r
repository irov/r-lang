module test.codegen.clone_failures;

/* R-OWN-0020 (L26): an allocation failure at any step of a clone destroys every component
   already built exactly once and leaves the source unchanged. The test wrapper makes the k-th
   allocating runtime step of the k-th attempt fail, so the attempts sweep every step; the
   program returns the number of failed attempts, which the wrapper compares with the number of
   steps it counted. */
struct Counter { atomic u32 drops; atomic u32 created; };
struct Tracked { arc Counter counter; i32 value; };
drop(Tracked* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
Tracked Tracked::clone(const Tracked* value) {
    const Counter* c = &*value->counter;
    core::atomic_fetch_add(&c->created, 1u32, core::memory_order::relaxed) as void;
    return Tracked {.counter = core::clone(&value->counter), .value = value->value};
}

struct Node {
    std.string::string name;
    list<std.string::string> tags;
    dict<u32, std.string::string> props;
    own Tracked* tracked;
    o<std.string::string> note;
};
Node Node::clone(const Node* value) throws std.alloc::alloc_error {
    return Node {.name = core::clone(&value->name),
                 .tags = core::clone(&value->tags),
                 .props = core::clone(&value->props),
                 .tracked = core::clone(&value->tracked),
                 .note = core::clone(&value->note)};
}

Node node(str name, u32 key, const (arc Counter)* counter) throws std.error::fault,
    std.list::push_error<std.string::string>, std.dict::insert_error<u32, std.string::string>,
    std.alloc::new_error<Tracked> {
    list<std.string::string> tags = std.list::create();
    std.string::string* first = tags.push_back(std.string::from_str("tag-a"));
    first as void;
    std.string::string* second = tags.push_back(std.string::from_str("tag-b"));
    second as void;
    dict<u32, std.string::string> props = std.dict::create();
    o<std.string::string> old = props.insert(key, std.string::from_str("value"));
    drop old;
    const Counter* c = &**counter;
    core::atomic_fetch_add(&c->created, 1u32, core::memory_order::relaxed) as void;
    own Tracked* tracked = std.alloc::try_new(Tracked {.counter = core::clone(counter), .value = 7});
    return Node {.name = std.string::from_str(name), .tags = move tags, .props = move props,
                 .tracked = move tracked, .note = o::some(std.string::from_str("note"))};
}

i32 run() throws TestAssertionFailed, std.error::fault, std.array::push_error<Node>,
    std.list::push_error<std.string::string>, std.dict::insert_error<u32, std.string::string>,
    std.alloc::new_error<Tracked> {
    arc Counter counter = new arc Counter {.drops = 0u32, .created = 0u32};
    array<Node> nodes = std.array::create();
    nodes.push(node("alpha", 1u32, &counter));
    nodes.push(node("beta", 2u32, &counter));
    i32 failures = 0;
    bool done = false;
    while (done == false) {
        try {
            array<Node> copy = core::clone(&nodes);
            if (len(copy) != 2usize || copy[1].name.len() != 4usize || len(copy[0].tags) != 2usize ||
                len(copy[1].props) != 1usize) {
                throw TestAssertionFailed {.code = 1};
            }
            drop copy;
            done = true;
        } catch (std.alloc::alloc_error failure) {
            failures += 1;
        }
    }
    if (len(nodes) != 2usize || nodes[0].name.len() != 5usize) { throw TestAssertionFailed {.code = 2}; }
    drop nodes;
    const Counter* c = &*counter;
    if (core::atomic_load(&c->created, core::memory_order::relaxed) !=
        core::atomic_load(&c->drops, core::memory_order::relaxed)) {
        throw TestAssertionFailed {.code = 3};
    }
    return failures;
}

i32 main() {
    try {
        return run();
    } catch (TestAssertionFailed failure) {
        return 100 + failure.code;
    } catch (std.array::push_error<Node> failure) {
        return 110;
    } catch (std.list::push_error<std.string::string> failure) {
        return 111;
    } catch (std.dict::insert_error<u32, std.string::string> failure) {
        return 112;
    } catch (std.alloc::new_error<Tracked> failure) {
        return 113;
    } catch (std.error::fault failure) {
        return 114;
    }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
