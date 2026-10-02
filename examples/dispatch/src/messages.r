module example.dispatch.messages;

enum Message {
    Status,
    Submit { std.string::string body; o<u32> retry; },
};

// The delivery policy holds the asynchronous step of a first submission and of a retry as
// async function values (Core R-TYPE-0054); the caller chooses the functions.
struct Delivery {
    async fn(std.string::string) -> std.string::string throws(std.alloc::alloc_error) first;
    async fn(std.string::string) -> std.string::string throws(std.alloc::alloc_error) retry;
};

async std.string::string deliver(std.string::string body) throws std.alloc::alloc_error {
    return f"delivered: {body}\n";
}

async std.string::string redeliver(std.string::string body) throws std.alloc::alloc_error {
    return f"redelivered: {body}\n";
}

async std.string::string process(Message message, Delivery delivery)
    throws std.alloc::alloc_error, std.async::start_error {
    return match (move message) {
        case variant Message::Status: std.string::from_str("dispatcher ready\n");
        case variant Message::Submit { .body = body, .retry = variant o::some(attempt) }
            if (attempt > 3u32): f"expired attempt={attempt}: {body}\n";
        case variant Message::Submit { .body = move body, .retry = variant o::some(attempt) }:
            await delivery.retry(move body);
        case variant Message::Submit { .body = move body, .retry = variant o::none }:
            await delivery.first(move body);
    };
}
