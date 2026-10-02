module example.generics.main;
import example.generics.vector::{Vector, singleton, empty, append, count};

i32 main() {
    try {
        Vector<i32> numbers = singleton(17);
        append(&numbers, 23);
        if (count(&numbers) != 2) { return 1; }
        Vector<i32> none = empty::<i32>();
        auto count_numbers = count::<i32>;
        if (count_numbers(&none) != 0 || count_numbers(&numbers) != 2) { return 6; }

        own i32* first = new i32(31);
        own i32* second = new i32(37);
        Vector<own i32*> owners = singleton(move first);
        append(&owners, move second);
        if (count(&owners) != 2) { return 2; }
    } catch (std.array::push_error<i32> failure) {
        return 4;
    } catch (std.array::push_error<own i32*> failure) {
        return 5;
    }
    return 0;
}
