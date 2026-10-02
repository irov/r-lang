module test.codegen.exclusive_view_holders;

/* R-BORROW-0002, R-BORROW-0004, R-BORROW-0018, R-BORROW-0019: exclusive borrows and mutable
   slices of values that hold borrows as locals, results and parameters; stores through them,
   through a method result and through an element borrow reach the storage they designate. */

struct Holder { str name; i32 id; };
struct Owner { array<str> names; Holder main; };

Holder* pick(Holder* left, Holder* right, bool take_left) {
    if (take_left == true) { return left; }
    return right;
}
str[] tail(str[] words) { return words[1usize..len(words)]; }
array<str>* Owner::names_mut(Owner* this) { return &this->names; }
Holder* Owner::main_mut(Owner* this) { return &this->main; }
void rename(Holder* holder) { holder->name = "renamed"; }
usize count(const Holder[] holders) {
    usize total = 0usize;
    for (usize index = 0usize; index < len(holders); index += 1usize) { total += len(holders[index].name); }
    return total;
}
void fill(str[] words, (str)* last) {
    words[0] = "zero";
    *last = "last";
}

usize run(str text) throws std.array::push_error<str>, std.list::push_error<str> {
    Holder a = {.name = "a", .id = 1};
    Holder b = {.name = "bb", .id = 2};
    Holder* chosen = pick(&a, &b, false);
    chosen->name = text;
    Holder* direct = &a;
    direct->name = "aaa";
    rename(direct);
    str s = "s";
    (str)* sp = &s;
    *sp = text;
    str[3] local = {"one", "two", "three"};
    str[] rest = tail(local[0usize..3usize]);
    rest[0] = text;
    fill(local[0usize..3usize], &s);
    Owner owner = {.names = std.array::create::<str>(), .main = {.name = "m", .id = 3}};
    array<str>* names = owner.names_mut();
    std.array::push(names, text);
    std.array::push(owner.names_mut(), "fixed");
    Holder* m = owner.main_mut();
    m->name = text;
    list<str> items = std.list::create::<str>();
    *std.list::push_back(&items, "x") = text;
    Holder[2] pair = {a, b};
    usize total = count(pair[0usize..2usize]);
    return total + len(b.name) + len(s) + len(local[0]) + len(local[1]) + len(owner.names) + len(owner.main.name) + len(items);
}

i32 main() {
    try {
        usize result = run("text");
        if (result != 11usize + 4usize + 4usize + 4usize + 4usize + 2usize + 4usize + 1usize) { return 1; }
    } catch (std.array::push_error<str> e) { e as void; return 2; }
    catch (std.list::push_error<str> e) { e as void; return 3; }
    return 0;
}
