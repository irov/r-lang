module test.codegen.library_xml;

import std.xml;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


bool same(str left, str right) {
    const u8[] a = left;
    const u8[] b = right;
    return std.bytes::equal(a, b);
}

/* Feeds the remaining document until the next event; end reports event_kind::none. */
std.xml::event_kind next_event(std.xml::reader* parser, const u8[] document, usize* fed, usize chunk)
    throws std.xml::error, std.alloc::alloc_error {
    while (true) {
        usize stop = *fed + chunk;
        if (stop > len(document)) { stop = len(document); }
        const u8[] piece = document[*fed..stop];
        std.xml::progress step = parser->feed(piece, stop == len(document));
        *fed += step.consumed;
        if (step.state == std.xml::state::event_ready) { return parser->kind(); }
        if (step.state == std.xml::state::end) { return std.xml::event_kind::none; }
    }
}

/* A digest of the event stream that does not depend on how the input was split. */
u32 digest(const u8[] document, usize chunk) throws std.xml::error, std.alloc::alloc_error {
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    usize fed = 0usize;
    u32 total = 17u32;
    while (true) {
        std.xml::event_kind kind = next_event(&parser, document, &fed, chunk);
        if (kind == std.xml::event_kind::none) { return total; }
        total = core::wrapping_mul_u32(total, 31u32);
        total = core::wrapping_add_u32(total, (core::enum_ordinal(kind) as u32) + 1u32);
        const u8[] name = parser.name();
        const u8[] text = parser.text();
        total = core::wrapping_add_u32(total, core::wrapping_mul_u32(std.hash::crc32(name), 3u32));
        total = core::wrapping_add_u32(total, core::wrapping_mul_u32(std.hash::crc32(text), 5u32));
        total = core::wrapping_add_u32(total, (parser.attribute_count() as u32) * 7u32);
        for (usize index = 0usize; index < parser.attribute_count(); index += 1usize) {
            const u8[] attribute_name = parser.attribute_name(index);
            const u8[] attribute_value = parser.attribute_value(index);
            total = core::wrapping_add_u32(total, std.hash::crc32(attribute_name));
            total = core::wrapping_add_u32(total, std.hash::crc32(attribute_value));
        }
        const u8[] space = parser.namespace();
        total = core::wrapping_add_u32(total, std.hash::crc32(space));
    }
}

i32 expect_error(const u8[] document, std.xml::error_code expected) throws std.alloc::alloc_error {
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    usize fed = 0usize;
    try {
        while (true) {
            std.xml::event_kind kind = next_event(&parser, document, &fed, 4096usize);
            if (kind == std.xml::event_kind::none) { return 1; }
        }
    } catch (std.xml::error failure) {
        if (failure.code != expected) { return 2; }
        bool poisoned = false;
        try {
            std.xml::progress again = parser.feed(document, true);
            again as void;
        } catch (std.xml::error second) { poisoned = second.code == std.xml::error_code::poisoned; }
        if (poisoned == false) { return 3; }
        return 0;
    }
}

i32 checks() throws std.xml::error, std.alloc::alloc_error {
    str document_text = "<?xml version='1.0' encoding='UTF-8'?>\n<!-- top -->\n<root xmlns='urn:r' xmlns:p='urn:p' a='1'>\n  <p:item id='1'>alpha &amp; &#x41;&#66;</p:item>\n  <item id='2' p:lang='en'><![CDATA[<raw>]]><name>beta</name></item>\n  <empty/>\n  <?proc data here?>\n</root>\n";
    const u8[] document = document_text;
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    usize fed = 0usize;
    std.xml::event_kind kind = next_event(&parser, document, &fed, 4096usize);
    if (kind != std.xml::event_kind::declaration) { return 1; }
    if (same(parser.text(), "version='1.0' encoding='UTF-8'") == false) { return 2; }
    std.xml::event_kind kind_2 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_2 != std.xml::event_kind::comment) || (same(parser.text(), " top ") == false)) { return 3; }
    std.xml::event_kind kind_3 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_3 != std.xml::event_kind::start_element) || (same(parser.name(), "root") == false)) { return 4; }
    if ((parser.attribute_count() != 3usize) || (parser.depth() != 1usize)) { return 5; }
    if (same(parser.namespace(), "urn:r") == false) { return 6; }
    o<str> a = parser.attribute("a");
    switch (a) {
    case variant o::some(value): if (same(*value, "1") == false) { return 7; } break;
    case variant o::none: return 8;
    }
    std.xml::selector items = std.xml::selector::compile("//item");
    std.xml::selector second = std.xml::selector::compile("/root/item[@id='2'][@p:lang]");
    test_observe(&second);
    std.xml::selector names = std.xml::selector::compile("item/name");
    test_observe(&names);
    std.xml::selector anything = std.xml::selector::compile("/*/*");
    test_observe(&anything);
    std.xml::selector wrong_root = std.xml::selector::compile("/item");
    test_observe(&wrong_root);
    if (items.matches(&parser) == true) { return 9; }
    std.xml::event_kind kind_4 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_4 != std.xml::event_kind::start_element) || (same(parser.name(), "p:item") == false)) { return 10; }
    if ((same(parser.prefix(), "p") == false) || (same(parser.local_name(), "item") == false)) { return 11; }
    if (same(parser.namespace(), "urn:p") == false) { return 12; }
    if ((items.matches(&parser) == true) || (anything.matches(&parser) == false) || (wrong_root.matches(&parser) == true)) { return 13; }
    std.xml::event_kind kind_5 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_5 != std.xml::event_kind::text) || (same(parser.text(), "alpha & AB") == false)) { return 14; }
    std.xml::event_kind kind_6 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_6 != std.xml::event_kind::end_element) || (same(parser.name(), "p:item") == false)) { return 15; }
    std.xml::event_kind kind_7 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_7 != std.xml::event_kind::start_element) || (same(parser.name(), "item") == false)) { return 16; }
    if ((items.matches(&parser) == false) || (second.matches(&parser) == false) || (names.matches(&parser) == true)) { return 17; }
    if (same(parser.namespace(), "urn:r") == false) { return 18; }
    std.xml::event_kind kind_8 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_8 != std.xml::event_kind::cdata) || (same(parser.text(), "<raw>") == false)) { return 19; }
    std.xml::event_kind kind_9 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_9 != std.xml::event_kind::start_element) || (same(parser.name(), "name") == false)) { return 20; }
    if ((names.matches(&parser) == false) || (parser.depth() != 3usize) || (same(parser.path_name(0usize), "root") == false)) { return 21; }
    std.xml::event_kind kind_10 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_10 != std.xml::event_kind::text) || (same(parser.text(), "beta") == false)) { return 22; }
    std.xml::event_kind kind_11 = next_event(&parser, document, &fed, 4096usize);
    if (kind_11 != std.xml::event_kind::end_element) { return 23; }
    std.xml::event_kind kind_12 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_12 != std.xml::event_kind::end_element) || (same(parser.name(), "item") == false)) { return 24; }
    std.xml::event_kind kind_13 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_13 != std.xml::event_kind::start_element) || (same(parser.name(), "empty") == false)) { return 25; }
    std.xml::event_kind kind_14 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_14 != std.xml::event_kind::end_element) || (same(parser.name(), "empty") == false)) { return 26; }
    std.xml::event_kind kind_15 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_15 != std.xml::event_kind::instruction) || (same(parser.name(), "proc") == false) || (same(parser.text(), "data here") == false)) { return 27; }
    std.xml::event_kind kind_16 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_16 != std.xml::event_kind::end_element) || (same(parser.name(), "root") == false) || (parser.depth() != 0usize)) { return 28; }
    std.xml::event_kind kind_17 = next_event(&parser, document, &fed, 4096usize);
    if ((kind_17 != std.xml::event_kind::none) || (fed != len(document)) || (parser.offset() != len(document))) { return 29; }
    bool finished = false;
    try {
        std.xml::progress late = parser.feed(document, true);
        late as void;
    } catch (std.xml::error failure) { finished = failure.code == std.xml::error_code::finished; }
    if (finished == false) { return 30; }

    /* Splitting the input at every byte or every third byte yields the same events. */
    u32 whole = digest(document, 4096usize);
    if ((digest(document, 1usize) != whole) || (digest(document, 3usize) != whole)) { return 31; }
    parser.reset();
    usize fed_2 = 0usize;
    std.xml::event_kind kind_18 = next_event(&parser, document, &fed_2, 7usize);
    if (kind_18 != std.xml::event_kind::declaration) { return 32; }

    /* Whitespace text is reported when asked for. */
    std.xml::options verbose = std.xml::default_options();
    verbose.skip_whitespace = false;
    std.xml::reader spaced = std.xml::reader::create(verbose);
    str small_text = "<a> <b/> </a>";
    const u8[] small = small_text;
    usize fed_3 = 0usize;
    std.xml::event_kind kind_19 = next_event(&spaced, small, &fed_3, 4096usize);
    kind_19 as void;
    std.xml::event_kind kind_20 = next_event(&spaced, small, &fed_3, 4096usize);
    if ((kind_20 != std.xml::event_kind::text) || (same(spaced.text(), " ") == false)) { return 33; }

    /* Selector diagnostics. */
    bool rejected = false;
    try {
        std.xml::selector bad = std.xml::selector::compile("a/[@x]");
        (move bad) as void;
    } catch (std.xml::error failure) {
        rejected = (failure.code == std.xml::error_code::invalid_selector) && (failure.offset == 2usize);
    }
    if (rejected == false) { return 34; }
    bool rejected_2 = false;
    try {
        std.xml::selector bad = std.xml::selector::compile("a[@x]/b");
        (move bad) as void;
    } catch (std.xml::error failure) { rejected_2 = failure.code == std.xml::error_code::invalid_selector; }
    if (rejected_2 == false) { return 35; }

    /* Reader diagnostics, each poisoning the reader. */
    str unbalanced_text = "<a><b></a>";
    const u8[] unbalanced = unbalanced_text;
    if (expect_error(unbalanced, std.xml::error_code::unbalanced) != 0) { return 36; }
    str doctype_text = "<!DOCTYPE a><a/>";
    const u8[] doctype = doctype_text;
    if (expect_error(doctype, std.xml::error_code::unsupported) != 0) { return 37; }
    str entity_text = "<a>&nbsp;</a>";
    const u8[] entity = entity_text;
    if (expect_error(entity, std.xml::error_code::unsupported) != 0) { return 38; }
    str duplicate_text = "<a x='1' x='2'/>";
    const u8[] duplicate = duplicate_text;
    if (expect_error(duplicate, std.xml::error_code::duplicate_attribute) != 0) { return 39; }
    str truncated_text = "<a><b>text";
    const u8[] truncated = truncated_text;
    if (expect_error(truncated, std.xml::error_code::unbalanced) != 0) { return 40; }
    str garbage_text = "<a/>tail";
    const u8[] garbage = garbage_text;
    if (expect_error(garbage, std.xml::error_code::malformed) != 0) { return 41; }
    u8[7] bad_bytes = { 60, 97, 62, 0xff, 60, 47, 97 };
    const u8[] bad_view = &bad_bytes;
    if (expect_error(bad_view, std.xml::error_code::invalid_utf8) != 0) { return 42; }
    str latin_text = "<?xml version='1.0' encoding='ISO-8859-1'?><a/>";
    const u8[] latin = latin_text;
    if (expect_error(latin, std.xml::error_code::unsupported) != 0) { return 43; }
    std.xml::options shallow = std.xml::default_options();
    shallow.max_depth = 2usize;
    std.xml::reader limited = std.xml::reader::create(shallow);
    str deep_text = "<a><b><c/></b></a>";
    const u8[] deep = deep_text;
    bool too_deep = false;
    usize fed_4 = 0usize;
    try {
        while (true) {
            kind_20 = next_event(&limited, deep, &fed_4, 4096usize);
            if (kind_20 == std.xml::event_kind::none) { break; }
        }
    } catch (std.xml::error failure) { too_deep = failure.code == std.xml::error_code::depth_limit; }
    if (too_deep == false) { return 44; }

    /* Writer: compact and pretty documents, escaping, and a round trip through the reader. */
    std.xml::writer out = std.xml::writer::create(false);
    out.declaration();
    out.start("root");
    out.attribute("a", "x<y&\"z\"");
    out.start("item");
    out.attribute("id", "1");
    out.text("alpha & <beta>");
    out.end();
    out.start("empty");
    out.end();
    out.comment(" note ");
    out.instruction("proc", "data");
    out.start("raw");
    out.cdata("<raw>");
    out.end();
    out.end();
    array<u8> compact = (move out).finish();
    const u8[] compact_view = std.array::as_slice(&compact);
    str expected_compact = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root a=\"x&lt;y&amp;&quot;z&quot;\"><item id=\"1\">alpha &amp; &lt;beta&gt;</item><empty/><!-- note --><?proc data?><raw><![CDATA[<raw>]]></raw></root>";
    const u8[] expected_compact_view = expected_compact;
    if (std.bytes::equal(compact_view, expected_compact_view) == false) { return 45; }
    std.xml::reader again = std.xml::reader::create(std.xml::default_options());
    usize fed_5 = 0usize;
    usize events = 0usize;
    while (true) {
        kind_20 = next_event(&again, compact_view, &fed_5, 5usize);
        if (kind_20 == std.xml::event_kind::none) { break; }
        events += 1usize;
        if ((kind_20 == std.xml::event_kind::start_element) && (same(again.name(), "root") == true)) {
            o<str> a_value = again.attribute("a");
            switch (a_value) {
            case variant o::some(value): if (same(*value, "x<y&\"z\"") == false) { return 46; } break;
            case variant o::none: return 47;
            }
        }
    }
    if (events != 13usize) { return 48; }
    std.xml::writer pretty = std.xml::writer::create(true);
    pretty.start("a");
    pretty.start("b");
    pretty.text("t");
    pretty.end();
    pretty.start("c");
    pretty.end();
    pretty.end();
    array<u8> shaped = (move pretty).finish();
    const u8[] shaped_view = std.array::as_slice(&shaped);
    str expected_pretty = "<a>\n  <b>t</b>\n  <c/>\n</a>\n";
    const u8[] expected_pretty_view = expected_pretty;
    if (std.bytes::equal(shaped_view, expected_pretty_view) == false) { return 49; }
    std.xml::writer misplaced = std.xml::writer::create(false);
    bool misplaced_seen = false;
    try { misplaced.attribute("a", "b"); }
    catch (std.xml::error failure) { misplaced_seen = failure.code == std.xml::error_code::misplaced; }
    if (misplaced_seen == false) { return 50; }
    misplaced.start("open");
    bool unbalanced_seen = false;
    try {
        array<u8> partial = (move misplaced).finish();
        (move partial) as void;
    } catch (std.xml::error failure) { unbalanced_seen = failure.code == std.xml::error_code::unbalanced; }
    if (unbalanced_seen == false) { return 51; }
    return 0;
}

i32 main() {
    try {
        try {
            i32 status = checks();
            return status;
        } catch (std.xml::error failure) { return 100 + (core::enum_ordinal(failure.code) as i32); }
        catch (std.alloc::alloc_error failure) { failure as void; throw TestAssertionFailed {.code = 90}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
