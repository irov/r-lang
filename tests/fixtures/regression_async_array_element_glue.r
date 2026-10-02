module test.regression.async_array_element_glue;

/* M32T-4: a standard call in an async frame that creates an owner of elements with drop glue,
   here std.array::create::<entry>(), names the move and drop gates of the element in the
   runtime description of the array, as such a call in an ordinary function does (L22-2). */
struct entry { std.string::string id; };

async i32 main() {
    array<entry> items = std.array::create::<entry>();
    usize count = len(items);
    drop items;
    if (count == 0usize) { return 0; }
    return 1;
}
