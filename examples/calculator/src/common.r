module example.calculator.common;

error Usage { constexpr str message; };

void require_operands(const str[] operands, usize expected) throws Usage {
    throw (len(operands) != expected) Usage { .message = "wrong number of operands" };
}

