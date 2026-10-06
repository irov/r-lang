module test.codegen.user_attributes;

/* R-AGG-0013 and R-REFL-0005 (L42): attribute types declared by the program, used on a type, its
   fields and the enumerators of an enum, and read at translation time, also from a generic. */

enum weight { light, heavy };

@attribute(type) struct table { str name; };
@attribute(field) struct key {};
@attribute(field) struct column {
    str name = "";
    i32 width = -1;
    bool indexed = false;
    weight load = weight::light;
    f64 scale = 1.0;
    char mark = 'x';
    u8 small = 7u8;
};
@attribute(variant) struct label { str text; };

@table("players")
struct Player {
    @key i64 id;
    @column(name = "nick", width = 32, indexed = true, load = weight::heavy, scale = -2.5, mark = 'n') str nickname;
    i32 score;
};

enum color { @label("Red") red, green, @label(text = "Blue") blue };

usize text_length(str text) { return len(text); }

/* The columns of any struct: the key field, then each column name or the field name. */
@generic<T>
usize column_letters() {
    usize total = 0usize;
    for (usize index = 0usize; index < core::field_count::<T>(); index += 1usize) {
        o<column> named = core::field_attribute::<column, T>(index);
        o<key> keyed = core::field_attribute::<key, T>(index);
        if (keyed is variant o::some(_)) { total += 100usize; }
        switch (named) {
            case variant o::some(c): {
                total += text_length(c->name);
            }
            case variant o::none: {
                total += text_length(core::field_name::<T>(index));
            }
        }
    }
    return total;
}

i32 main() {
    o<table> where = core::type_attribute::<table, Player>();
    switch (where) {
        case variant o::some(t): {
            if (text_length(t->name) != 7usize) { return 1; }
        }
        case variant o::none: { return 2; }
    }
    o<table> nothing = core::type_attribute::<table, color>();
    if (nothing is variant o::some(_)) { return 3; }
    o<column> nick = core::field_attribute::<column, Player>(1usize);
    switch (nick) {
        case variant o::some(c): {
            if (c->width != 32) { return 4; }
            if (c->indexed != true) { return 5; }
            if (c->load != weight::heavy) { return 6; }
            if (c->scale != -2.5) { return 7; }
            if (c->mark != 'n') { return 8; }
            if (c->small != 7u8) { return 9; }
        }
        case variant o::none: { return 10; }
    }
    usize far = 7usize;
    o<column> beyond = core::field_attribute::<column, Player>(far);
    if (beyond is variant o::some(_)) { return 11; }
    if (text_length(core::field_name::<Player>(far)) != 0usize) { return 12; }
    usize middle = 2usize;
    if (text_length(core::field_name::<Player>(middle)) != 5usize) { return 13; }
    o<label> red = core::variant_attribute::<label, color>(color::red);
    if (red is variant o::none) { return 14; }
    o<label> green = core::variant_attribute::<label, color>(color::green);
    if (green is variant o::some(_)) { return 15; }
    o<label> blue = core::variant_attribute::<label, color>(color::blue);
    switch (blue) {
        case variant o::some(b): {
            if (text_length(b->text) != 4usize) { return 16; }
        }
        case variant o::none: { return 17; }
    }
    /* id: key (100) + "id" (2); nickname: "nick" (4); score: "score" (5) */
    if (column_letters::<Player>() != 111usize) { return 18; }
    return 0;
}
