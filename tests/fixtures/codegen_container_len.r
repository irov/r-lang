module test.codegen.container_len;
/* R-ARRAY-0004: len counts the live entries of a dict and the live nodes of a list. */
i32 main() {
    try {
        dict<i32, i32> d = std.dict::create::<i32, i32>();
        usize n0 = len(d);
        if (n0 != 0usize) { return 1; }
        o<i32> previous = std.dict::insert(&d, 1, 10);
        previous as void;
        o<i32> again = std.dict::insert(&d, 2, 20);
        again as void;
        usize n1 = len(d);
        if (n1 != 2usize) { return 2; }
        list<i32> l = std.list::create::<i32>();
        i32* p1 = std.list::push_back(&l, 5);
        i32 v1 = *p1;
        v1 as void;
        i32* p2 = std.list::push_back(&l, 6);
        i32 v2 = *p2;
        v2 as void;
        i32* p3 = std.list::push_back(&l, 7);
        i32 v3 = *p3;
        v3 as void;
        usize n2 = len(l);
        if (n2 != 3usize) { return 3; }
    } catch (std.dict::insert_error<i32, i32> f) {
        f as void;
        return 4;
    } catch (std.list::push_error<i32> g) {
        g as void;
        return 5;
    }
    return 0;
}
