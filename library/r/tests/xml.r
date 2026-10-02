module tests.std.xml;
import std.test;
import std.xml;

// The tests of std.xml (Library R-SLIB-XML-0001..0006), run in test mode (Core R-FUNC-0025).

protected const str document_text =
    "<?xml version='1.0' encoding='UTF-8'?>\n<!-- top -->\n"
    "<root xmlns='urn:r' xmlns:p='urn:p' a='1'>\n"
    "  <p:item id='1'>alpha &amp; &#x41;&#66;</p:item>\n"
    "  <item id='2' p:lang='en'><![CDATA[<raw>]]><name>beta</name></item>\n"
    "  <empty/>\n  <?proc data here?>\n</root>\n";

/* The events of document_text, one per `|`. */
protected const str document_events =
    "?xml(version='1.0' encoding='UTF-8')|!-- top --|<root xmlns=urn:r xmlns:p=urn:p a=1>|"
    "<p:item id=1>|\"alpha & AB\"|</p:item>|<item id=2 p:lang=en>|[CDATA[<raw>]]|<name>|"
    "\"beta\"|</name>|</item>|<empty>|</empty>|?proc(data here)|</root>|";

/* Feeds the document in pieces of `chunk` bytes until the next event; none at its end. */
protected std.xml::event_kind next_event(std.xml::reader* parser, const u8[] document,
                                         usize* fed, usize chunk)
    throws std.xml::error, std.alloc::alloc_error {
    while (true) {
        usize stop = *fed + chunk;
        if (stop > len(document)) { stop = len(document); }
        std.xml::progress step = parser->feed(document[*fed..stop], stop == len(document));
        *fed += step.consumed;
        if (step.state == std.xml::state::event_ready) { return parser->kind(); }
        if (step.state == std.xml::state::end) { return std.xml::event_kind::none; }
    }
    return std.xml::event_kind::none;
}

/* Appends the current event of the reader to the text. The accessor results are bound to
   locals because a call argument of an R function does not select an append overload. */
protected void append_event(std.string::string* text, const std.xml::reader* parser)
    throws std.alloc::alloc_error {
    switch (parser->kind()) {
    case std.xml::event_kind::declaration: {
        str content = parser->text();
        text->append("?xml(");
        text->append(content);
        text->append(")");
    }
    case std.xml::event_kind::start_element: {
        str name = parser->name();
        text->append("<");
        text->append(name);
        for (usize index = 0usize; index < parser->attribute_count(); index += 1usize) {
            str attribute_name = parser->attribute_name(index);
            str attribute_value = parser->attribute_value(index);
            text->append(" ");
            text->append(attribute_name);
            text->append("=");
            text->append(attribute_value);
        }
        text->append(">");
    }
    case std.xml::event_kind::end_element: {
        str name = parser->name();
        text->append("</");
        text->append(name);
        text->append(">");
    }
    case std.xml::event_kind::text: {
        str content = parser->text();
        text->append("\"");
        text->append(content);
        text->append("\"");
    }
    case std.xml::event_kind::cdata: {
        str content = parser->text();
        text->append("[CDATA[");
        text->append(content);
        text->append("]]");
    }
    case std.xml::event_kind::comment: {
        str content = parser->text();
        text->append("!--");
        text->append(content);
        text->append("--");
    }
    case std.xml::event_kind::instruction: {
        str name = parser->name();
        str content = parser->text();
        text->append("?");
        text->append(name);
        text->append("(");
        text->append(content);
        text->append(")");
    }
    case std.xml::event_kind::none: text->append("none");
    }
    text->append("|");
}

/* The events of the document read with the options in pieces of `chunk` bytes. */
protected std.string::string describe(const u8[] document, std.xml::options settings,
                                      usize chunk)
    throws std.xml::error, std.alloc::alloc_error {
    std.xml::reader parser = std.xml::reader::create(settings);
    std.string::string text = std.string::create();
    usize fed = 0usize;
    while (true) {
        std.xml::event_kind kind = next_event(&parser, document, &fed, chunk);
        if (kind == std.xml::event_kind::none) { break; }
        append_event(&text, &parser);
    }
    return move text;
}

/* The number of start_element events that the selector matches. */
protected usize count_matches(str pattern, const u8[] document)
    throws std.xml::error, std.alloc::alloc_error {
    std.xml::selector wanted = std.xml::selector::compile(pattern);
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    usize fed = 0usize;
    usize count = 0usize;
    while (true) {
        std.xml::event_kind kind = next_event(&parser, document, &fed, 4096usize);
        if (kind == std.xml::event_kind::none) { break; }
        if (wanted.matches(&parser) == true) { count += 1usize; }
    }
    drop wanted;
    return count;
}

/* Fails unless reading the document reports the code, after which the reader is poisoned. */
protected void expect_read_error(str label, const u8[] document, std.xml::options settings,
                                 std.xml::error_code code)
    throws std.test::failure, std.alloc::alloc_error {
    std.xml::reader parser = std.xml::reader::create(settings);
    usize fed = 0usize;
    try {
        while (true) {
            std.xml::event_kind kind = next_event(&parser, document, &fed, 4096usize);
            if (kind == std.xml::event_kind::none) { break; }
        }
        std.string::string message = f"{label}: the document was read";
        std.test::fail(message.as_str());
    } catch (std.xml::error failure) {
        std.string::string message = f"{label}: the error code";
        std.test::check(failure.code == code, message.as_str());
        try {
            parser.feed(document, true) as void;
            std.test::fail("a poisoned reader continued");
        } catch (std.xml::error second) {
            std.test::check(second.code == std.xml::error_code::poisoned, "poisoned");
        }
    }
}

/* Fails unless the bytes are the UTF-8 text. */
protected void expect_bytes(const array<u8>* actual, str expected)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        str text = core::validate_utf8(std.array::as_slice(actual));
        std.test::equal_text(text, expected);
    } catch (core::utf8_error failure) {
        failure as void;
        std.test::fail("the bytes are not UTF-8");
    }
}

@test
void reads_events_in_document_order()
    throws std.test::failure, std.alloc::alloc_error, std.xml::error {
    std.string::string events = describe(document_text, std.xml::default_options(), 4096usize);
    std.test::equal_text(events.as_str(), document_events);
    // The accessors of single events.
    const u8[] document = document_text;
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    usize fed = 0usize;
    for (usize skip = 0usize; skip < 3usize; skip += 1usize) {
        next_event(&parser, document, &fed, 4096usize) as void;
    }
    std.test::equal_text(parser.name(), "root");
    std.test::equal(parser.depth(), 1usize);
    std.test::equal_text(parser.namespace(), "urn:r");
    std.test::equal(parser.attribute_count(), 3usize);
    o<str> a = parser.attribute("a");
    switch (a) {
    case variant o::some(value): std.test::equal_text(*value, "1");
    case variant o::none: std.test::fail("root has the attribute a");
    }
    o<str> missing = parser.attribute("b");
    switch (missing) {
    case variant o::some(value): std.test::fail("root has no attribute b");
    case variant o::none: break;
    }
    next_event(&parser, document, &fed, 4096usize) as void;
    std.test::equal_text(parser.name(), "p:item");
    std.test::equal_text(parser.prefix(), "p");
    std.test::equal_text(parser.local_name(), "item");
    std.test::equal_text(parser.namespace(), "urn:p");
    std.test::equal(parser.depth(), 2usize);
    std.test::equal_text(parser.path_name(0usize), "root");
    std.test::equal_text(parser.path_name(1usize), "p:item");
    // The text, the end of p:item, the start of item and the CDATA section come before name.
    for (usize skip = 0usize; skip < 5usize; skip += 1usize) {
        next_event(&parser, document, &fed, 4096usize) as void;
    }
    std.test::equal_text(parser.name(), "name");
    std.test::equal(parser.depth(), 3usize);
    std.test::equal_text(parser.path_name(1usize), "item");
    std.test::equal_text(parser.namespace(), "urn:r");
    for (usize skip = 0usize; skip < 7usize; skip += 1usize) {
        next_event(&parser, document, &fed, 4096usize) as void;
    }
    std.test::check(parser.kind() == std.xml::event_kind::end_element, "the end of root");
    std.test::equal(parser.depth(), 0usize);
    std.xml::event_kind last = next_event(&parser, document, &fed, 4096usize);
    std.test::check(last == std.xml::event_kind::none, "the document ends");
    std.test::equal(parser.offset(), len(document));
    // A step after the end is an error.
    try {
        parser.feed(document, true) as void;
        std.test::fail("a finished reader continued");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::finished, "finished");
    }
}

@test
void accepts_fragments_split_anywhere()
    throws std.test::failure, std.alloc::alloc_error, std.xml::error {
    usize[5] chunks = {1usize, 2usize, 3usize, 7usize, 64usize};
    for (usize index = 0usize; index < 5usize; index += 1usize) {
        std.string::string events =
            describe(document_text, std.xml::default_options(), chunks[index]);
        std.test::equal_text(events.as_str(), document_events);
    }
    // A reset reader starts a new document.
    const u8[] document = document_text;
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    usize fed = 0usize;
    for (usize skip = 0usize; skip < 5usize; skip += 1usize) {
        next_event(&parser, document, &fed, 5usize) as void;
    }
    parser.reset();
    std.test::equal(parser.depth(), 0usize);
    std.test::equal(parser.offset(), 0usize);
    usize again = 0usize;
    std.xml::event_kind first = next_event(&parser, document, &again, 9usize);
    std.test::check(first == std.xml::event_kind::declaration, "the declaration comes first");
}

@test
void decodes_text_and_attributes()
    throws std.test::failure, std.alloc::alloc_error, std.xml::error {
    std.string::string entities = describe(
        "<a t='x&lt;y'>&lt;&gt;&amp;&quot;&apos;&#65;&#x42;&#x20AC;</a>",
        std.xml::default_options(), 4096usize);
    std.test::equal_text(entities.as_str(), "<a t=x<y>|\"<>&\"'AB€\"|</a>|");
    // Tab, LF and CR in attribute values become spaces.
    std.string::string normalized =
        describe("<a v='1\t2\n3\r4'/>", std.xml::default_options(), 4096usize);
    std.test::equal_text(normalized.as_str(), "<a v=1 2 3 4>|</a>|");
    // Whitespace-only text is dropped unless asked for; CDATA stays verbatim.
    str spaced = "<a> <b/> <![CDATA[ &amp; ]]></a>";
    std.string::string skipped = describe(spaced, std.xml::default_options(), 4096usize);
    std.test::equal_text(skipped.as_str(), "<a>|<b>|</b>|[CDATA[ &amp; ]]|</a>|");
    std.xml::options verbose = std.xml::default_options();
    verbose.skip_whitespace = false;
    std.string::string kept = describe(spaced, verbose, 4096usize);
    std.test::equal_text(kept.as_str(), "<a>|\" \"|<b>|</b>|\" \"|[CDATA[ &amp; ]]|</a>|");
    std.xml::options defaults = std.xml::default_options();
    std.test::equal(defaults.max_depth, 256usize);
    std.test::equal(defaults.max_attributes, 256usize);
    std.test::check(defaults.skip_whitespace == true, "whitespace is skipped by default");
}

@test
void selects_elements_by_path()
    throws std.test::failure, std.alloc::alloc_error, std.xml::error {
    str library =
        "<library><shelf id='a'><book lang='en'><title>T</title></book></shelf>"
        "<book lang='fr'/><shelf><box><book/></box></shelf></library>";
    std.test::equal(count_matches("//book", library), 3usize);
    std.test::equal(count_matches("book", library), 3usize);
    std.test::equal(count_matches("/library/book", library), 1usize);
    std.test::equal(count_matches("/book", library), 0usize);
    std.test::equal(count_matches("shelf/book", library), 1usize);
    std.test::equal(count_matches("shelf//book", library), 2usize);
    std.test::equal(count_matches("/library//box/*", library), 1usize);
    std.test::equal(count_matches("/*/*", library), 3usize);
    std.test::equal(count_matches("book[@lang]", library), 2usize);
    std.test::equal(count_matches("book[@lang='fr']", library), 1usize);
    std.test::equal(count_matches("shelf[@id=\"a\"]", library), 1usize);
    std.test::equal(count_matches("book[@lang][@lang='de']", library), 0usize);
    std.test::equal(count_matches("title", library), 1usize);
}

@test(allocations)
void writes_escaped_documents()
    throws std.test::failure, std.alloc::alloc_error, std.xml::error {
    std.xml::writer out = std.xml::writer::create(false);
    out.declaration();
    out.start("root");
    out.attribute("a", "x<y&\"z\"");
    out.start("item");
    out.text("alpha & <beta>");
    out.end();
    std.test::equal(out.depth(), 1usize);
    out.start("empty");
    out.end();
    out.comment(" note ");
    out.instruction("proc", "data");
    out.start("raw");
    out.cdata("<raw>");
    out.end();
    out.end();
    std.test::equal(out.depth(), 0usize);
    array<u8> compact = (move out).finish();
    expect_bytes(&compact,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root a=\"x&lt;y&amp;&quot;z&quot;\">"
                 "<item>alpha &amp; &lt;beta&gt;</item><empty/><!-- note --><?proc data?>"
                 "<raw><![CDATA[<raw>]]></raw></root>");
    // The reader reads back what the writer wrote.
    std.string::string events =
        describe(std.array::as_slice(&compact), std.xml::default_options(), 4096usize);
    std.test::equal_text(events.as_str(),
                         "?xml(version=\"1.0\" encoding=\"UTF-8\")|<root a=x<y&\"z\">|<item>|"
                         "\"alpha & <beta>\"|</item>|<empty>|</empty>|!-- note --|?proc(data)|"
                         "<raw>|[CDATA[<raw>]]|</raw>|</root>|");
    // The pretty form: one element per line, two spaces per depth, text inline, a final LF.
    std.xml::writer pretty = std.xml::writer::create(true);
    pretty.start("a");
    pretty.start("b");
    pretty.text("t");
    pretty.end();
    pretty.start("c");
    pretty.end();
    pretty.end();
    array<u8> shaped = (move pretty).finish();
    expect_bytes(&shaped, "<a>\n  <b>t</b>\n  <c/>\n</a>\n");
}

@test(expect = std.xml::error)
void rejects_an_unbalanced_end_tag() throws std.xml::error, std.alloc::alloc_error {
    std.string::string events =
        describe("<a><b></a>", std.xml::default_options(), 4096usize);
    drop events;
}

@test
void reports_reader_errors() throws std.test::failure, std.alloc::alloc_error {
    std.xml::options defaults = std.xml::default_options();
    expect_read_error("doctype", "<!DOCTYPE a><a/>", defaults, std.xml::error_code::unsupported);
    expect_read_error("entity", "<a>&nbsp;</a>", defaults, std.xml::error_code::unsupported);
    expect_read_error("encoding", "<?xml version='1.0' encoding='ISO-8859-1'?><a/>", defaults,
                      std.xml::error_code::unsupported);
    expect_read_error("character zero", "<a>&#0;</a>", defaults,
                      std.xml::error_code::unsupported);
    expect_read_error("duplicate", "<a x='1' x='2'/>", defaults,
                      std.xml::error_code::duplicate_attribute);
    expect_read_error("mismatched end", "<a><b></c></a>", defaults,
                      std.xml::error_code::unbalanced);
    expect_read_error("truncated", "<a><b>text", defaults, std.xml::error_code::unbalanced);
    expect_read_error("text after the root", "<a/>tail", defaults,
                      std.xml::error_code::malformed);
    expect_read_error("empty document", "", defaults, std.xml::error_code::malformed);
    u8[8] bad_bytes = {60, 97, 62, 0xff, 60, 47, 97, 62};
    expect_read_error("invalid UTF-8", bad_bytes, defaults, std.xml::error_code::invalid_utf8);
    std.xml::options shallow = std.xml::default_options();
    shallow.max_depth = 2usize;
    expect_read_error("depth", "<a><b><c/></b></a>", shallow, std.xml::error_code::depth_limit);
    std.xml::options narrow = std.xml::default_options();
    narrow.max_attributes = 1usize;
    expect_read_error("attributes", "<a x='1' y='2'/>", narrow,
                      std.xml::error_code::attribute_limit);
}

@test
void reports_writer_and_selector_errors() throws std.test::failure, std.alloc::alloc_error {
    std.xml::writer out = std.xml::writer::create(false);
    try {
        out.attribute("a", "b");
        std.test::fail("an attribute outside a start tag");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::misplaced, "attribute");
    }
    try {
        out.end();
        std.test::fail("an end without an open element");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::misplaced, "end");
    }
    out.start("open");
    try {
        out.declaration();
        std.test::fail("a declaration after output");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::misplaced, "declaration");
    }
    try {
        out.comment("a--b");
        std.test::fail("a comment with --");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::malformed, "comment");
    }
    try {
        out.cdata("x]]>y");
        std.test::fail("CDATA with its terminator");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::malformed, "cdata");
    }
    try {
        out.instruction("proc", "a?>b");
        std.test::fail("instruction data with its terminator");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::malformed, "instruction");
    }
    out.text("t");
    try {
        out.attribute("late", "x");
        std.test::fail("an attribute after text");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::misplaced, "late attribute");
    }
    // The failed operations wrote nothing.
    std.test::check(std.bytes::equal(out.as_slice(), "<open>t") == true, "the output so far");
    try {
        array<u8> partial = (move out).finish();
        drop partial;
        std.test::fail("a document with an open element");
    } catch (std.xml::error failure) {
        std.test::check(failure.code == std.xml::error_code::unbalanced, "unbalanced");
    }
    str[6] patterns = {"", "a/", "a/[@x]", "a[@x]/b", "a[x]", "a b"};
    usize[6] offsets = {0usize, 2usize, 2usize, 5usize, 2usize, 1usize};
    for (usize index = 0usize; index < 6usize; index += 1usize) {
        try {
            std.xml::selector wrong = std.xml::selector::compile(patterns[index]);
            drop wrong;
            std.test::fail("an invalid selector compiled");
        } catch (std.xml::error failure) {
            std.test::check(failure.code == std.xml::error_code::invalid_selector, "selector");
            std.test::equal(failure.offset, offsets[index]);
        }
    }
}

/* M24-7: the whole document through one reader; true when it ends, false on its first error. */
protected bool well_formed(str document) throws std.alloc::alloc_error {
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    const u8[] bytes = document;
    usize fed = 0usize;
    try {
        while (true) {
            std.xml::progress step = parser.feed(bytes[fed..len(bytes)], true);
            fed += step.consumed;
            if (step.state == std.xml::state::end) { return true; }
        }
    } catch (std.xml::error failure) {
        failure as void;
        return false;
    }
    return false;
}

/* M24-7: a character reference names a character of XML 1.0, and character data holds no
   `]]>`; the escaped form `]]&gt;` stays allowed. */
@test
void rejects_what_xml_excludes() throws std.test::failure, std.alloc::alloc_error {
    std.test::check(well_formed("<a>&#1;</a>") == false, "&#1; is no XML character");
    std.test::check(well_formed("<a>&#xFFFE;</a>") == false, "&#xFFFE; is no XML character");
    std.test::check(well_formed("<a>x]]>y</a>") == false, "]]> in character data");
    std.test::check(well_formed("<a>&#9;&#xA;&#x10000;</a>") == true, "tab, LF and U+10000");
    std.test::check(well_formed("<a>]]&gt; ]&gt; ]]</a>") == true, "escaped and short brackets");
}
