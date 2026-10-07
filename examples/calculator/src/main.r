module example.calculator.main;

import std.console;
import std.text;
import example.calculator.common::{Usage};
import example.calculator.expression;
import example.calculator.expression::{Syntax};
import example.calculator.operations::{Operation};
import example.calculator.number_f32;
import example.calculator.number_f64;
import example.calculator.number_c_float;
import example.calculator.number_c_double;
import example.calculator.number_c_long_double;
import example.calculator.number_complex_f32;
import example.calculator.number_complex_f64;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string result; i32 exit_status; };

protected std.string::string calculate(str... words)
    throws Usage, Syntax, core::recursion_error, std.convert::parse_error, std.math::math_error,
        std.alloc::alloc_error {
    throw (len(words) < 2usize) Usage { .message = "usage: calculator OP TYPE OPERANDS..." };
    switch (words[0]) {
    case "eval":
        // An arithmetic expression, evaluated by a parser whose recursion is bounded (L35).
        throw (len(words) != 2usize) Usage { .message = "usage: calculator eval EXPRESSION" };
        f64 value = example.calculator.expression::evaluate(words[1]);
        std.string::string text = f"{value}\n";
        return move text;
    case "rpn":
        // A postfix expression, read by a state machine on a labeled switch (L46).
        throw (len(words) != 2usize) Usage { .message = "usage: calculator rpn EXPRESSION" };
        f64 value = example.calculator.expression::postfix(words[1]);
        std.string::string text = f"{value}\n";
        return move text;
    default:
    }
    Operation operation = match (core::enum_from_name::<Operation>(words[0])) {
        case variant o::some(value): value;
        case variant o::none: throw Usage { .message = "unknown operation" };
    };
    // The type name selects its module; letters compare without ASCII case (L20).
    std.text::ascii_caseless selector = std.text::ignore_ascii_case(words[1]);
    const str[] operands = words[2..];
    return match (selector) {
        case "f32": example.calculator.number_f32::evaluate(operation, ...operands);
        case "f64": example.calculator.number_f64::evaluate(operation, ...operands);
        case "c_float": example.calculator.number_c_float::evaluate(operation, ...operands);
        case "c_double": example.calculator.number_c_double::evaluate(operation, ...operands);
        case "c_long_double": example.calculator.number_c_long_double::evaluate(operation, ...operands);
        case "complex_f32": example.calculator.number_complex_f32::evaluate(operation, ...operands);
        case "complex_f64": example.calculator.number_complex_f64::evaluate(operation, ...operands);
        default: throw Usage { .message = "unknown number type" };
    };
}

async i32 main(const str[] arguments) {
    CommandResponse response = {.result = std.string::create(), .exit_status = 0};

    try {
        if (len(arguments) == 1usize) {
            response.result = std.string::from_str("calculator OP TYPE OPERANDS...\ncalculator eval EXPRESSION\ncalculator rpn EXPRESSION\nTypes: f32 f64 c_float c_double c_long_double complex_f32 complex_f64\nExamples: sqrt f64 9; hypot f32 3 4; mul complex_f64 1 2 3 4; eval \"2 * (3 + 4)\"\n");
        } else {
            // Every word after the program name: the operation, the type and the operands.
            response.result = calculate(...arguments[1..]);
        }
    } catch (Usage failure) {
        response.result = f"{failure.message}\n";
        response.exit_status = 64;
    } catch (Syntax failure) {
        response.result = f"syntax error at byte {failure.offset}\n";
        response.exit_status = 65;
    } catch (core::recursion_error failure) {
        response.result = f"expression nests deeper than {failure.depth} levels\n";
        response.exit_status = 65;
    } catch (std.convert::parse_error failure) {
        std.error::error diagnostic = std.error::from_parse(failure);
        response.result = diagnostic.diagnostic();
        response.exit_status = 65;
    } catch (std.math::math_error failure) {
        std.math::error_code code = failure.code;
        if (code == std.math::error_code::domain) {
            response.result = std.string::from_str("operand is outside the mathematical domain\n");
        } else {
            std.error::error diagnostic = std.math::as_error(failure);
            response.result = diagnostic.diagnostic();
        }
        response.exit_status = 65;
    }
    await std.console::print(core::replace(&response.result, std.string::create()));
    return response.exit_status;
}
