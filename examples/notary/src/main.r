module example.notary.main;
import std.console;
import std.encoding;
import std.cbor;
import std.crypto;
import std.cose;
import example.notary.keys;
import example.notary.tools;

/* notary signs and seals short messages as COSE messages (RFC 9052) with keys derived from a
   passphrase, verifies and opens them, and shows them in CBOR diagnostic notation. */
enum Command { public_key, sign, verify, show, seal, open, exchange, password, digest, tag, document, demo };

error Usage { u32 code; };

protected const str usage_text =
    "notary public_key PASSPHRASE | sign PASSPHRASE MESSAGE | verify PUBLIC MESSAGE_HEX\n"
    "notary show MESSAGE_HEX | seal PASSPHRASE TEXT | open PASSPHRASE MESSAGE_HEX\n"
    "notary exchange NOTE | password SECRET | digest TEXT | tag KEY MESSAGE | document | demo\n";

str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index].as_str();
}

protected bytes hex_argument(const array<std.string::string>* arguments, usize index)
    throws Usage, std.alloc::alloc_error {
    try {
        return std.encoding::decode_hex(word(arguments, index));
    } catch (std.convert::parse_error failure) {
        failure as void;
    }
    throw Usage {.code = 3u32};
}

protected std.string::string text_of(const u8[] data) throws std.alloc::alloc_error {
    try {
        return std.string::from_str(core::validate_utf8(data));
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    return std.encoding::encode_hex(data);
}

/* A COSE_Sign1 message of `message`, with the key identifier "notary". */
std.string::string signed(str passphrase, str message)
    throws std.alloc::alloc_error, std.crypto::crypto_error, std.cose::cose_error {
    std.crypto::signing_key key = example.notary.keys::signer(passphrase);
    std.cose::headers fields = std.cose::headers::create();
    fields.protect(std.cose::ALGORITHM, std.cbor::value::integer(std.cose::EDDSA));
    fields.expose(std.cose::KEY_ID, std.cbor::value::of_bytes("notary"));
    bytes sealed = std.cose::sign1(move fields, message, &key, "");
    return std.encoding::encode_hex(sealed.as_slice());
}

/* The diagnostic notation of any CBOR item. */
std.string::string shown(const u8[] data) throws std.alloc::alloc_error, std.cbor::cbor_error {
    std.cbor::value item = std.cbor::decode(data);
    return std.cbor::diagnostic(&item);
}

/* A COSE_Encrypt0 message of `text` under the sealing key, with a random IV. */
std.string::string sealed_text(str passphrase, str text)
    throws std.alloc::alloc_error, std.crypto::crypto_error, std.cose::cose_error {
    std.secret::buffer key = example.notary.keys::sealing_key(passphrase);
    bytes iv = std.crypto::random(12usize);
    bytes message = std.cose::encrypt0(std.cose::headers::create(), text, std.secret::as_slice(&key),
                                       iv.as_slice(), "");
    return std.encoding::encode_hex(message.as_slice());
}

protected async void say(std.string::string text) throws std.error::fault {
    await std.console::println(move text);
}

protected void line(std.string::string* out, std.string::string text) throws std.alloc::alloc_error {
    std.string::append_str(out, text.as_str());
    std.string::push_scalar(out, '\n');
}

/* Signs a message, verifies it, shows it, and shows that a changed message and another key
   are refused; then seals and opens a text. */
std.string::string demo() throws std.alloc::alloc_error, std.crypto::crypto_error, std.cose::cose_error,
    std.cbor::cbor_error, std.convert::parse_error {
    std.string::string out = std.string::create();
    str passphrase = "correct horse battery staple";
    std.crypto::signing_key key = example.notary.keys::signer(passphrase);
    std.string::string public_hex = std.encoding::encode_hex(key.public_key[..]);
    line(&out, f"public key {public_hex}");
    std.string::string message_hex = signed(passphrase, "Pay 10 coins to Bob");
    bytes message = std.encoding::decode_hex(message_hex.as_str());
    usize size = len(message);
    line(&out, f"signed {size} bytes");
    line(&out, shown(message.as_slice()));
    // Reading a message without verifying it gives its headers, here the key identifier.
    std.cose::message read = std.cose::decode(message.as_slice(), std.cose::SIGN1);
    switch (read.fields.find(std.cose::KEY_ID)) {
    case variant o::some(found):
        switch (**found) {
        case variant std.cbor::value::bytes(kid):
            std.string::string kid_text = text_of(std.array::as_slice(kid));
            line(&out, f"key id: {kid_text}");
        default: line(&out, std.string::from_str("key id: not bytes"));
        }
    case variant o::none: line(&out, std.string::from_str("key id: none"));
    }
    // A raw Ed25519 signature of the same text, without COSE.
    u8[64] raw_signature = key.sign("Pay 10 coins to Bob");
    bool raw_valid = std.crypto::verify(key.public_key[..], "Pay 10 coins to Bob", raw_signature[..]);
    line(&out, f"raw signature valid: {raw_valid}");
    std.cose::message checked = std.cose::verify_sign1(message.as_slice(), key.public_key[..], "");
    switch (checked.content) {
    case variant o::some(payload):
        std.string::string text = text_of(std.array::as_slice(payload));
        line(&out, f"verified: {text}");
    case variant o::none: line(&out, std.string::from_str("verified: detached payload"));
    }
    // Change the last byte of the payload: the signature no longer covers it.
    message[size - 67usize] = (message[size - 67usize] ^ 1u8) as u8;
    try {
        std.cose::message forged = std.cose::verify_sign1(message.as_slice(), key.public_key[..], "");
        drop forged;
        line(&out, std.string::from_str("changed message accepted"));
    } catch (std.cose::cose_error failure) {
        std.cose::error_code code = failure.code;
        line(&out, f"changed message refused: {code}");
    }
    std.crypto::signing_key other = example.notary.keys::signer("another passphrase");
    bytes original = std.encoding::decode_hex(message_hex.as_str());
    try {
        std.cose::message foreign = std.cose::verify_sign1(original.as_slice(), other.public_key[..], "");
        drop foreign;
        line(&out, std.string::from_str("other key accepted"));
    } catch (std.cose::cose_error failure) {
        std.cose::error_code code = failure.code;
        line(&out, f"other key refused: {code}");
    }
    std.string::string sealed_hex = sealed_text(passphrase, "meet at noon");
    bytes sealed = std.encoding::decode_hex(sealed_hex.as_str());
    std.secret::buffer sealing = example.notary.keys::sealing_key(passphrase);
    bytes opened = std.cose::decrypt0(sealed.as_slice(), std.secret::as_slice(&sealing), "");
    std.string::string opened_text = text_of(opened.as_slice());
    line(&out, f"opened: {opened_text}");
    return move out;
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1].as_str()); }
    switch (command) {
    case variant o::none:
        await std.console::eprint(std.string::from_str(usage_text));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        try {
            switch (*selected) {
            case Command::public_key:
                throw (given != 3usize) Usage {.code = 2u32};
                std.crypto::signing_key key = example.notary.keys::signer(word(&arguments, 2usize));
                await say(std.encoding::encode_hex(key.public_key[..]));
            case Command::sign:
                throw (given != 4usize) Usage {.code = 2u32};
                await say(signed(word(&arguments, 2usize), word(&arguments, 3usize)));
            case Command::verify:
                throw (given != 4usize) Usage {.code = 2u32};
                bytes public_key = hex_argument(&arguments, 2usize);
                bytes message = hex_argument(&arguments, 3usize);
                try {
                    std.cose::message checked = std.cose::verify_sign1(message.as_slice(), public_key.as_slice(), "");
                    switch (checked.content) {
                    case variant o::some(payload):
                        std.string::string text = text_of(std.array::as_slice(payload));
                        await say(f"valid: {text}");
                    case variant o::none: await say(std.string::from_str("valid: detached payload"));
                    }
                } catch (std.cose::cose_error failure) {
                    std.cose::error_code code = failure.code;
                    await say(f"invalid: {code}");
                    return 65;
                }
            case Command::show:
                throw (given != 3usize) Usage {.code = 2u32};
                bytes message = hex_argument(&arguments, 2usize);
                await say(shown(message.as_slice()));
            case Command::seal:
                throw (given != 4usize) Usage {.code = 2u32};
                await say(sealed_text(word(&arguments, 2usize), word(&arguments, 3usize)));
            case Command::open:
                throw (given != 4usize) Usage {.code = 2u32};
                bytes message = hex_argument(&arguments, 3usize);
                std.secret::buffer key = example.notary.keys::sealing_key(word(&arguments, 2usize));
                try {
                    bytes opened = std.cose::decrypt0(message.as_slice(), std.secret::as_slice(&key), "");
                    await say(text_of(opened.as_slice()));
                } catch (std.cose::cose_error failure) {
                    std.cose::error_code code = failure.code;
                    await say(f"invalid: {code}");
                    return 65;
                }
            case Command::exchange:
                throw (given != 3usize) Usage {.code = 2u32};
                await std.console::print(example.notary.tools::exchange(word(&arguments, 2usize)));
            case Command::password:
                throw (given != 3usize) Usage {.code = 2u32};
                await std.console::print(example.notary.tools::password(word(&arguments, 2usize)));
            case Command::digest:
                throw (given != 3usize) Usage {.code = 2u32};
                await std.console::print(example.notary.tools::digests(word(&arguments, 2usize)));
            case Command::tag:
                throw (given != 4usize) Usage {.code = 2u32};
                await std.console::print(example.notary.tools::tagged(word(&arguments, 2usize), word(&arguments, 3usize)));
            case Command::document:
                throw (given != 2usize) Usage {.code = 2u32};
                await std.console::print(example.notary.tools::document());
            case Command::demo:
                throw (given != 2usize) Usage {.code = 2u32};
                await std.console::print(demo());
            }
            return 0;
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.cbor::cbor_error failure) {
            std.cbor::error_code code = failure.code;
            await std.console::eprintln(f"not CBOR: {code}");
            return 65;
        } catch (std.cose::cose_error failure) {
            std.cose::error_code code = failure.code;
            await std.console::eprintln(f"refused: {code}");
            return 65;
        } catch (std.crypto::crypto_error failure) {
            std.crypto::error_code code = failure.code;
            await std.console::eprintln(f"crypto failure: {code}");
            return 70;
        } catch (std.convert::parse_error failure) {
            failure as void;
            return 70;
        }
    }
    return 70;
}
