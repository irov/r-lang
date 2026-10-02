module test.codegen.async_consteval_floats;

/* L22.2 (R-EXPR-0032): translation-time floating values inside asynchronous functions, lowered
   through MIR. */

struct Sample { f32 gain; f64 offset; };

f64 kelvin(f64 celsius) { return celsius + 273.15; }

Sample calibrate(f32 gain) {
    Sample sample = Sample {.gain = gain * 2.0f32, .offset = kelvin(-273.15)};
    return sample;
}

f64[4] table() {
    f64[4] values = {};
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        values[index] = std.math::floor_f64((index as f64) * 1.5);
    }
    return values;
}

const f64 FREEZING = kelvin(0.0);
const Sample SAMPLE = calibrate(0.75f32);
const f64[4] TABLE = table();

async f64 warm(f64 degrees) {
    f64 folded = kelvin(25.0);
    return folded + degrees;
}

async i32 main() {
    i32 failures = 0;
    failures += FREEZING == 273.15 ? 0 : 1;
    failures += SAMPLE.gain == 1.5f32 && SAMPLE.offset == 0.0 ? 0 : 1;
    failures += TABLE[3] == 4.0 && TABLE[1] == 1.0 ? 0 : 1;
    f64 value = await warm(1.0);
    failures += value == 299.15 ? 0 : 1;
    return failures;
}
