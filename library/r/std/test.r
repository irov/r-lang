module std.test;
import std.cmp;
import std.text;

/* R-SLIB-TEST-0001: the failure of an assertion, with the text that explains it. */
error failure {
    std.string::string message;
};

/* R-SLIB-TEST-0001: fails with the message unless the condition holds. */
void check(bool condition, str message) throws failure, std.alloc::alloc_error {
    if (condition == true) { return; }
    throw failure {.message = std.string::from_str(message)};
}

/* R-SLIB-TEST-0001: fails with the message. */
void fail(str message) throws failure, std.alloc::alloc_error {
    throw failure {.message = std.string::from_str(message)};
}

/* R-SLIB-TEST-0001: fails with `expected E, got A` unless the two values are equal. */
@generic<T: std.cmp::Equal & core::Format>
void equal(T actual, T expected) throws failure, std.alloc::alloc_error {
    if (actual.eq(&expected) == true) { return; }
    throw failure {.message = f"expected {expected}, got {actual}"};
}

/* R-SLIB-TEST-0001: fails with `expected a value other than U` when the two values are equal. */
@generic<T: std.cmp::Equal & core::Format>
void not_equal(T actual, T unexpected) throws failure, std.alloc::alloc_error {
    if (actual.eq(&unexpected) == false) { return; }
    throw failure {.message = f"expected a value other than {unexpected}"};
}

/* R-SLIB-TEST-0001: fails with `expected "E", got "A"` unless the texts are equal. */
void equal_text(str actual, str expected) throws failure, std.alloc::alloc_error {
    if (std.bytes::equal(actual, expected) == true) { return; }
    throw failure {.message = f"expected \"{expected}\", got \"{actual}\""};
}

/* R-SLIB-TEST-0001: fails with `expected "T" to contain "P"` unless the part occurs in the text. */
void contains(str text, str part) throws failure, std.alloc::alloc_error {
    if (std.text::contains(text, part) == true) { return; }
    throw failure {.message = f"expected \"{text}\" to contain \"{part}\""};
}

/* R-SLIB-TEST-0002: the account that the test entry keeps: the test that runs, the first problem
   of it, the allocation attempt that fails, and the counts. */
struct runner {
    protected std.string::string problem;
    protected bool failing;
    protected u64 attempt;
    protected u64 attempts;
    protected u64 passed;
    protected u64 failed;
};

/* R-SLIB-TEST-0002: an account without tests. */
runner runner::create() {
    return runner {.problem = std.string::create(), .failing = false, .attempt = 0u64,
                   .attempts = 0u64, .passed = 0u64, .failed = 0u64};
}

/* R-SLIB-TEST-0002: begins the test `name` and returns `test NAME ... `. */
std.string::string runner::start(runner* this, str name) throws std.alloc::alloc_error {
    this->problem.clear();
    this->failing = false;
    this->attempt = 0u64;
    this->attempts = 0u64;
    return f"test {name} ... ";
}

/* The first problem of the test counts; a problem under an injected allocation failure names the
   attempt. */
protected void runner::record(runner* this, std.string::string text) throws std.alloc::alloc_error {
    if (this->failing == true) { return; }
    this->failing = true;
    this->problem.append(text.as_str());
    if (this->attempt != 0u64) {
        std.string::string suffix = f" (allocation {this->attempt} failing)";
        this->problem.append(suffix.as_str());
    }
}

/* R-SLIB-TEST-0002: the test failed an assertion. */
void runner::fail(runner* this, const failure* reason) throws std.alloc::alloc_error {
    this->record(std.string::from_str(reason->message.as_str()));
}

/* R-SLIB-TEST-0002: the test threw a standard error; the problem is its portable name. */
void runner::fault(runner* this, std.error::fault error) throws std.alloc::alloc_error {
    auto name = std.error::name(std.error::from_fault(error));
    this->record(f"error {name}");
}

/* R-SLIB-TEST-0002: the test threw another error; the problem is the name of its type. */
@generic<E>
void runner::unexpected(runner* this, E error) throws std.alloc::alloc_error {
    str type = core::type_name::<E>();
    this->record(f"error {type}");
}

/* R-SLIB-TEST-0002: the test threw the error it expects, so it passes. */
@generic<E>
void runner::expected(runner* this, E error) {
}

/* R-SLIB-TEST-0002: the test returned instead of throwing the error it expects. */
void runner::missing(runner* this, str expected) throws std.alloc::alloc_error {
    this->record(f"expected the error {expected}");
}

/* R-SLIB-TEST-0002: the allocation attempt that fails in the next run of the test, zero for
   none; the attempts count those of the run without failures. */
void runner::inject(runner* this, u64 attempt, u64 attempts) {
    this->attempt = attempt;
    this->attempts = attempts;
}

/* R-SLIB-TEST-0002: whether the test has no problem so far. */
bool runner::passing(const runner* this) {
    return this->failing == false;
}

/* R-SLIB-TEST-0002: ends the test and returns `ok`, `ok (N allocation failures)` or
   `FAILED: PROBLEM`, with a line feed. */
std.string::string runner::finish(runner* this) throws std.alloc::alloc_error {
    if (this->failing == true) {
        this->failed += 1u64;
        return f"FAILED: {this->problem}\n";
    }
    this->passed += 1u64;
    if (this->attempts != 0u64) {
        return f"ok ({this->attempts} allocation failures)\n";
    }
    return std.string::from_str("ok\n");
}

/* R-SLIB-TEST-0002: `N tests: P passed, F failed` with a line feed. */
std.string::string runner::summary(const runner* this) throws std.alloc::alloc_error {
    u64 total = this->passed + this->failed;
    return f"{total} tests: {this->passed} passed, {this->failed} failed\n";
}

/* R-SLIB-TEST-0002: zero when every test passed, otherwise one. */
i32 runner::status(const runner* this) {
    if (this->failed == 0u64) { return 0; }
    return 1;
}
