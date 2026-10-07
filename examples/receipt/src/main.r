module example.receipt.main;
import std.console;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

protected std.string::string receipt(str customer, i32 quantity, f64 unit_price)
    throws Usage, std.alloc::alloc_error, std.format::format_error {
    bool finite = std.math::is_finite(unit_price);
    throw (quantity < 0 || unit_price < 0.0 || finite == false) Usage { .message = "quantity and price must be finite and nonnegative" };
    f64 total = (quantity as f64) * unit_price;
    bool total_finite = std.math::is_finite(total);
    throw (total_finite == false) Usage { .message = "receipt total overflow" };
    std.string::string captured = std.string::from_str(customer);
    std.format::format row = f"{captured}: {1} x {2:.2} = {3:.2} ({1} units)";
    drop captured;
    std.string::string sale = row.format(quantity, unit_price, total);
    std.string::string refund = row.format(0, unit_price, 0.0);

    std.format::builder builder = std.format::with_capacity(64usize);
    builder.append("Receipt for ");
    builder.append(customer);
    builder.append('\n');
    str heading = builder;
    std.string::string output = std.string::from_str(heading);
    // The copied heading remains valid while this allocation is reused for the body.
    builder.clear();
    str sale_view = sale;
    builder.append(sale_view);
    builder.append('\n');
    builder.append("Empty-order preview: ");
    str refund_view = refund;
    builder.append(refund_view);
    builder.append('\n');
    builder.append("Raw total: ");
    builder.append(total);
    builder.append('\n');
    std.string::string body = (move builder).finish();
    str body_view = body;
    output.append(body_view);
    return move output;
}

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) { response.output = std.string::from_str("receipt CUSTOMER QUANTITY UNIT_PRICE\n"); }
        else {
            throw (count != 4usize) Usage { .message = "expected a customer, quantity and unit price" };
            i32 quantity = std.convert::parse_i32(arguments[2], 10u32);
            f64 price = std.convert::parse_f64(arguments[3]);
            response.output = receipt(arguments[1], quantity, price);
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.format::format_error failure) {
        std.error::error error = std.error::from_format(failure);
        response.output = error.diagnostic(); response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
