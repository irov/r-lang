module test.composed_membership;
i32 read(i32* calls, i32 value) { *calls += 1; return value; }
i32 counted(i32* calls) { *calls += 1; return *calls; }
i32 main() {
    i32 calls = 0;
    bool inside = read(&calls, 5) in read(&calls, 0)..read(&calls, 10);
    if (inside == false || calls != 3) { return 1; }
    bool below = read(&calls, -1) in read(&calls, 0)..read(&calls, 10);
    if (below == true || calls != 5) { return 2; }
    bool ordered = read(&calls, 5) in counted(&calls)..read(&calls, 10);
    if (ordered == true || calls != 7) { return 4; }
    i32 value = 0;
    bool once = read(&value, 0) in 0..1;
    if (once == false || value != 1) { return 3; }
    return 0;
}
