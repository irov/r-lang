module example.quotes.policy;

error PolicyError { InvalidRate, Disabled };

void validate() throws std.env::env_error, PolicyError {
    o<std.string::string> setting = std.env::get("R_QUOTES_ENABLED");
    switch (move setting) {
    case variant o::some(move value):
        str text = value.as_str();
        throw (std.bytes::equal(text, "yes") == false) PolicyError::Disabled;
        break;
    case variant o::none: break;
    }
}

u32 default_rate() throws std.env::env_error, std.convert::parse_error, PolicyError {
    o<std.string::string> setting = std.env::get("R_QUOTES_TAX");
    switch (move setting) {
    case variant o::some(move value):
        str text = value.as_str();
        u32 rate = std.convert::parse_u32(text, 10u32);
        throw (rate > 10000u32) PolicyError::InvalidRate;
        return rate;
    case variant o::none: return 2000u32;
    }
}
