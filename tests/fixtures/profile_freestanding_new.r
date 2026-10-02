module profile.freestanding_new;

i32 main() {
    own i32* value = new i32(7);
    i32 result = *value;
    drop value;
    return result - 7;
}
