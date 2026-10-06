module example.status.records;

/* The records of the report. Kind, Device and Reading derive their text from their fields;
   Celsius writes its own. */

@derive(format)
enum Kind { sensor, relay, gateway };

@derive(format)
struct Device { u32 id; Kind kind; std.net::socket_address endpoint; o<u32> firmware; };

struct Celsius { f64 degrees; };

impl core::Format for Celsius {
    void format(const Celsius* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string text = f"{this->degrees} C";
        std.format::append_str(out, text);
    }
};

@derive(format)
enum Reading { temperature(Celsius), switched { u32 relay; bool on; }, missing };

std.net::socket_address endpoint(str address, u16 port) throws std.error::fault {
    std.net::ip_address ip = std.net::parse_ip(address);
    return std.net::socket_address {.address = ip, .port = port, .scope_id = 0u32};
}

Device[3] fleet() throws std.error::fault {
    Device[3] members = {
        Device {.id = 1u32, .kind = Kind::sensor, .endpoint = endpoint("10.0.0.7", 5683u16),
                .firmware = o::some(12u32)},
        Device {.id = 2u32, .kind = Kind::relay, .endpoint = endpoint("10.0.0.9", 502u16),
                .firmware = o::none},
        Device {.id = 3u32, .kind = Kind::gateway, .endpoint = endpoint("fd00::1", 8883u16),
                .firmware = o::some(4u32)},
    };
    return members;
}

Reading[3] readings() {
    Reading[3] values = {Reading::temperature(Celsius {.degrees = 21.5}),
                         Reading::switched {.relay = 2u32, .on = true},
                         Reading::missing};
    return values;
}
