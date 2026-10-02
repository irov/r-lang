module regression.c_float_modulo;

i32 main() {
    c_double left = 3.0 as c_double;
    c_double right = 2.0 as c_double;
    c_double result = left % right;
    result as void;
    return 0;
}
