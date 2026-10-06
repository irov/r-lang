module example.arena.mapping;
import std.postgres;

/* Attributes of the program (Core R-AGG-0013) that tie a struct to its table: the table on the
   type and the key on a field. The statements take both from them at translation time
   (R-REFL-0005) and the columns from the JSON names of the fields (Library R-SLIB-PG-0015). */

@attribute(type) struct table { str name; };
@attribute(field) struct key {};

/* The table that T names with @table, or the name of T. */
@generic<T>
protected std.string::string table_of() throws std.alloc::alloc_error {
    std.string::string name = std.string::create();
    o<table> named = core::type_attribute::<table, T>();
    switch (named) {
    case variant o::some(found): name.append(found->name);
    case variant o::none: name.append(core::type_name::<T>());
    }
    return move name;
}

/* The INSERT statement of T for its table. */
@generic<T: json_encode>
std.string::string insert_statement() throws std.postgres::pg_error, std.alloc::alloc_error {
    std.string::string name = table_of::<T>();
    return std.postgres::insert_statement::<T>(name);
}

/* The UPDATE statement of T for its table, keyed by the field marked @key. */
@generic<T: json_encode>
std.string::string update_statement() throws std.postgres::pg_error, std.alloc::alloc_error {
    std.string::string name = table_of::<T>();
    std.string::string column = std.string::create();
    for (usize index = 0usize; index < core::field_count::<T>(); index += 1usize) {
        o<key> marked = core::field_attribute::<key, T>(index);
        if (marked is variant o::some(_)) { column.append(core::field_name::<T>(index)); }
    }
    return std.postgres::update_statement::<T>(name, column);
}
