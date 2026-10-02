module test.codegen.async_chains;

import std.string;

/* R-FUNC-0024 (L19): chains in asynchronous frames, before an await and after one. */
i32 drop_count(bool bump) {
    static i32 count = 0;
    unsafe {
        if (bump == true) { count += 1; }
        return count;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

error Invalid { i32 code; };

struct Request { Token token; std.string::string path; i32 retries; };

Request Request::create(i32 id) {
    return Request { .token = Token { .id = id }, .path = std.string::create(), .retries = 0 };
}

@chain Request Request::with_path(Request this, str path)
    throws Invalid, std.alloc::alloc_error {
    throw (len(path) == 0usize) Invalid { .code = 1 };
    this.path = std.string::from_str(path);
    return move this;
}

@chain Request Request::with_retries(Request this, i32 retries) {
    this.retries = retries;
    return move this;
}

/* An asynchronous consuming method takes a directly moved owner (R-FUNC-0010). */
async i32 Request::send(Request this) {
    return this.token.id * 100 + (this.path.len() as i32) * 10 + this.retries;
}

async Request fetch(i32 id) { return Request::create(id); }

i32 Request::finish(Request this) { return this.token.id * 10 + this.retries; }

/* L19-2: a consuming receiver produced by a call or an await lives in the frame until the call
   takes it, with or without a chain. */
async i32 direct() throws std.async::start_error {
    i32 made = Request::create(6).finish();
    i32 awaited = (await fetch(7)).finish();
    return made * 100 + awaited;
}

async i32 build(constexpr str path)
    throws Invalid, std.alloc::alloc_error, std.async::start_error {
    Request first = Request::create(4).with_path(path).with_retries(2);
    i32 sent = await (move first).send();
    Request later = (await fetch(5)).with_retries(1).with_path("/x");
    i32 second = await (move later).send();
    return sent * 1000 + second;
}

struct Counter { i32 total; };

@discardable @chain Counter* Counter::add(Counter* this, i32 value) {
    this->total += value;
    return this;
}

async i32 count_up(i32 start) throws std.async::start_error {
    Counter counter = {.total = start};
    counter.add(1)->add(2);
    i32 before = counter.total;
    Request request = await fetch(9);
    counter.add((move request).with_retries(4).with_retries(before).retries);
    return counter.total;
}

async i32 main() {
    i32 failures = 0;
    try {
        failures += await build("/ab") == 432521 ? 0 : 1;
    } catch (Invalid failure) { failures += 2; }
    catch (std.alloc::alloc_error failure) { failures += 4; }
    catch (std.async::start_error failure) { failures += 4; }
    failures += drop_count(false) == 2 ? 0 : 8;
    try {
        failures += await build("") == 0 ? 16 : 32;
    } catch (Invalid failure) { failures += failure.code == 1 ? 0 : 64; }
    catch (std.alloc::alloc_error failure) { failures += 128; }
    catch (std.async::start_error failure) { failures += 128; }
    failures += drop_count(false) == 3 ? 0 : 256;
    failures += await count_up(10) == 26 ? 0 : 512;
    failures += drop_count(false) == 4 ? 0 : 1024;
    failures += await direct() == 6070 ? 0 : 2048;
    failures += drop_count(false) == 6 ? 0 : 4096;
    return failures;
}
