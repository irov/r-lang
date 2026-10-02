module audit.update_place_evaluated_once;

protected usize next_index(usize* calls) {
    *calls += 1;
    return 0;
}

i32 main() {
    i32[1] values = {1};
    usize calls = 0;

    values[next_index(&calls)]++;
    ++values[next_index(&calls)];
    values[next_index(&calls)] += 2;

    i32 selected = calls == 3 && values[0] == 5 ? 0 : 1;
    return selected;
}
