module test.codegen.async_view_holding_parameters;

/* R-BORROW-0018, R-BORROW-0024: a @scoped async frame takes a fixed array of views, an option
   of a view or a container iterator by value and keeps it across await; synchronous callees
   with such parameters run inside async frames. */

async void pause() {}

usize total((str)[2] words) {
    return len(words[0]) + len(words[1]);
}

i32 rest(std.list::iter<i32> cursor) {
    i32 sum = 0;
    while (true) {
        o<const i32*> item = std.list::next(&cursor);
        switch (item) {
            case variant o::some(value):
                sum += **value;
                break;
            case variant o::none:
                return sum;
        }
    }
}

@scoped
async usize measure((str)[2] words, o<str> extra) throws std.async::start_error {
    await pause();
    usize size = total(words);
    switch (extra) {
        case variant o::some(text):
            size += len(*text);
            break;
        case variant o::none:
            break;
    }
    return size;
}

@scoped
async i32 shift((i32*)[2] cells) throws std.async::start_error {
    await pause();
    *cells[0] += 10;
    *cells[1] += 20;
    return *cells[0] + *cells[1];
}

@scoped
async i32 drain(std.list::iter<i32> cursor, std.dict::iter<i32, i32> pairs)
    throws std.async::start_error {
    await pause();
    i32 sum = rest(move cursor);
    o<std.dict::entry_ref<i32, i32>> entry = std.dict::next(&pairs);
    await pause();
    switch (entry) {
        case variant o::some(found):
            sum += *(*found).value;
            break;
        case variant o::none:
            return sum - 100;
    }
    return sum;
}

async i32 main() {
    try {
        list<i32> numbers = std.list::create::<i32>();
        std.list::push_back(&numbers, 1) as void;
        std.list::push_back(&numbers, 2) as void;
        dict<i32, i32> table = std.dict::create::<i32, i32>();
        std.dict::insert(&table, 5, 40) as void;
        str text = "four";
        (str)[2] words = {"abc", "de"};
        i32 a = 1;
        i32 b = 2;
        task_scope(3) group {
            usize size = await measure(words, o::some(text));
            if (size != 9usize) {
                return 1;
            }
            (i32*)[2] cells = {&a, &b};
            i32 shifted = await shift(move cells);
            if (shifted != 33) {
                return 2;
            }
            i32 drained = await drain(std.list::iter(&numbers), std.dict::iter(&table));
            if (drained != 43) {
                return 3;
            }
        }
        if ((a != 11) || (b != 22)) {
            return 4;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    } catch (std.list::push_error<i32> failure) {
        move failure as void;
        return 91;
    } catch (std.dict::insert_error<i32, i32> failure) {
        move failure as void;
        return 92;
    }
}
