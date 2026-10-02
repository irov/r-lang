module test.regression.unreachable_helpers;

/* M32-3: functions that main never reaches hold a floating literal and a clone of a shared
   owner; the program uses neither, so the generated C defines no helper for them, which
   -Werror would reject as unused. */
struct counter { u32 hits; };

protected f64 unused_ratio() { return 1.5f64; }

protected f32 unused_scale() { return 0.25f32; }

protected arc counter unused_share(const (arc counter)* shared) { return std.arc::clone(shared); }

i32 main() { return 0; }
