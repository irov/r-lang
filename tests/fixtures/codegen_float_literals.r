module test.codegen.float_literals;

/* Each literal is returned at run time: a call with constant arguments would be replaced by its
   value during translation (R-EXPR-0032), and the wrapper calls the functions directly. */
thread_local bool selected = true;

protected f32 a_half_even(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 1.000000059604644775390625f32;
}

protected f32 b_half_up(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 1.0000000596046447753906251f32;
}

protected f32 c_hex_successor(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 0x1.000002p+0f32;
}

protected f32 d_min_subnormal(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 0x1p-149f32;
}

protected f32 e_positive_underflow(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 1e-100000f32;
}

protected f32 f_negative_underflow(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return -1e-100000f32;
}

protected f32 g_max_f32(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 340282346638528859811704183484516925440.0f32;
}

protected f64 h_hex_successor(bool literal) {
    if (literal == false) {
        return 0.0f64;
    }
    return 0x1.0000000000001p+0f64;
}

protected f64 i_max_f64(bool literal) {
    if (literal == false) {
        return 0.0f64;
    }
    return
        179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.0f64;
}

protected f64 j_default_f64(bool literal) {
    if (literal == false) {
        return 0.0f64;
    }
    return 1.5;
}

protected f32 k_digit_separators(bool literal) {
    if (literal == false) {
        return 0.0f32;
    }
    return 1_2.5_0f32;
}

i32 main() {
    f32 a_value = a_half_even(selected);
    f32 b_value = b_half_up(selected);
    f32 c_value = c_hex_successor(selected);
    f32 d_value = d_min_subnormal(selected);
    f32 e_value = e_positive_underflow(selected);
    f32 f_value = f_negative_underflow(selected);
    f32 g_value = g_max_f32(selected);
    f64 h_value = h_hex_successor(selected);
    f64 i_value = i_max_f64(selected);
    f64 j_value = j_default_f64(selected);
    f32 k_value = k_digit_separators(selected);

    a_value as void;
    b_value as void;
    c_value as void;
    d_value as void;
    e_value as void;
    f_value as void;
    g_value as void;
    h_value as void;
    i_value as void;
    j_value as void;
    k_value as void;

    return 0;
}
