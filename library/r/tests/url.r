module tests.std.url;
import std.test;
import std.url;

// The tests of std.url (Library R-SLIB-URL-0001..0005): parsing into parts, rejections with
// their offsets, recomposition, reference resolution with every example of RFC 3986 section 5.4,
// normalization, percent-encoding and form-encoded queries. Run in test mode (Core R-FUNC-0025).

/* A port, or zero for none. */
protected u32 port_of(o<u16> port) {
    switch (port) {
    case variant o::some(value): return *value as u32;
    case variant o::none: return 0u32;
    }
}

protected void expect_resolved(const std.url::url* base, str reference, str expected)
    throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.url::url target = base->resolve(reference);
    std.string::string text = target.text();
    std.test::equal_text(text, expected);
}

protected std.url::error_code rejection(str text) throws std.test::failure, std.alloc::alloc_error {
    try {
        std.url::url value = std.url::parse(text);
        drop value;
    } catch (std.url::url_error failure) {
        return failure.code;
    }
    std.test::fail("the text was accepted");
    return std.url::error_code::empty;
}

@test
void parses_the_parts_of_a_url() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.url::url value = std.url::parse("HTTP://User:pw@Example.COM:8080/a/b%2Fc?x=1&y#top");
    std.test::equal_text(value.scheme(), "http");
    std.test::check(value.has_authority(), "an authority");
    switch (value.userinfo()) {
    case variant o::some(info): std.test::equal_text(*info, "User:pw");
    case variant o::none: std.test::fail("userinfo");
    }
    std.test::equal_text(value.host(), "example.com");
    std.test::equal(port_of(value.port()), 8080u32);
    std.test::equal_text(value.path(), "/a/b%2Fc");
    switch (value.query()) {
    case variant o::some(query): std.test::equal_text(*query, "x=1&y");
    case variant o::none: std.test::fail("query");
    }
    switch (value.fragment()) {
    case variant o::some(fragment): std.test::equal_text(*fragment, "top");
    case variant o::none: std.test::fail("fragment");
    }
    std.string::string target = value.target();
    std.test::equal_text(target, "/a/b%2Fc?x=1&y");
    std.string::string text = value.text();
    std.test::equal_text(text, "http://User:pw@example.com:8080/a/b%2Fc?x=1&y#top");
    std.string::string shown = f"{value}";
    std.test::equal_text(shown, text);
}

@test
void parses_hosts_ports_and_schemes() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.url::url ipv6 = std.url::parse("https://[::1]:8443/");
    std.test::check(ipv6.is_ipv6(), "an IPv6 host");
    std.test::equal_text(ipv6.host(), "::1");
    std.string::string authority = ipv6.authority_text();
    std.test::equal_text(authority, "[::1]:8443");
    std.url::url plain = std.url::parse("https://example.org");
    std.test::equal(port_of(plain.port()), 0u32);
    std.test::equal(port_of(plain.effective_port()), 443u32);
    std.string::string target = plain.target();
    std.test::equal_text(target, "/");
    std.url::url mail = std.url::parse("mailto:someone@example.org");
    std.test::check(mail.has_authority() == false, "no authority");
    std.test::equal_text(mail.path(), "someone@example.org");
    std.test::equal(port_of(mail.effective_port()), 0u32);
    std.url::url urn = std.url::parse("urn:isbn:0451450523");
    std.test::equal_text(urn.path(), "isbn:0451450523");
    std.url::url empty_port = std.url::parse("http://host:/p");
    std.test::equal(port_of(empty_port.port()), 0u32);
    std.test::equal(port_of(std.url::default_port("WSS")), 443u32);
}

@test
void rejects_invalid_urls() throws std.test::failure, std.alloc::alloc_error {
    std.test::check(rejection("") == std.url::error_code::empty, "empty");
    std.test::check(rejection("/relative/path") == std.url::error_code::missing_scheme, "no scheme");
    std.test::check(rejection("1http://x") == std.url::error_code::invalid_scheme, "scheme");
    std.test::check(rejection("http://exa mple.com/") == std.url::error_code::invalid_character, "space");
    std.test::check(rejection("http://x/%zz") == std.url::error_code::invalid_percent_encoding, "triplet");
    std.test::check(rejection("http://x/%4") == std.url::error_code::invalid_percent_encoding, "short");
    std.test::check(rejection("http://x:99999/") == std.url::error_code::invalid_port, "port range");
    std.test::check(rejection("http://x:8a/") == std.url::error_code::invalid_port, "port digit");
    std.test::check(rejection("http://[::1/") == std.url::error_code::invalid_host, "bracket");
    std.test::check(rejection("http://[g::1]/") == std.url::error_code::invalid_host, "ipv6");
    try {
        std.url::url value = std.url::parse("http://x/a b");
        drop value;
        std.test::fail("accepted");
    } catch (std.url::url_error failure) {
        std.test::equal(failure.offset, 10usize);
        std.string::string shown = f"{failure.code}";
        std.test::equal_text(shown, "invalid_character");
    }
}

@test
void resolves_the_examples_of_rfc_3986() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.url::url base = std.url::parse("http://a/b/c/d;p?q");
    expect_resolved(&base, "g:h", "g:h");
    expect_resolved(&base, "g", "http://a/b/c/g");
    expect_resolved(&base, "./g", "http://a/b/c/g");
    expect_resolved(&base, "g/", "http://a/b/c/g/");
    expect_resolved(&base, "/g", "http://a/g");
    expect_resolved(&base, "//g", "http://g");
    expect_resolved(&base, "?y", "http://a/b/c/d;p?y");
    expect_resolved(&base, "g?y", "http://a/b/c/g?y");
    expect_resolved(&base, "#s", "http://a/b/c/d;p?q#s");
    expect_resolved(&base, "g#s", "http://a/b/c/g#s");
    expect_resolved(&base, "g?y#s", "http://a/b/c/g?y#s");
    expect_resolved(&base, ";x", "http://a/b/c/;x");
    expect_resolved(&base, "g;x", "http://a/b/c/g;x");
    expect_resolved(&base, "g;x?y#s", "http://a/b/c/g;x?y#s");
    expect_resolved(&base, "", "http://a/b/c/d;p?q");
    expect_resolved(&base, ".", "http://a/b/c/");
    expect_resolved(&base, "./", "http://a/b/c/");
    expect_resolved(&base, "..", "http://a/b/");
    expect_resolved(&base, "../", "http://a/b/");
    expect_resolved(&base, "../g", "http://a/b/g");
    expect_resolved(&base, "../..", "http://a/");
    expect_resolved(&base, "../../", "http://a/");
    expect_resolved(&base, "../../g", "http://a/g");
    expect_resolved(&base, "../../../g", "http://a/g");
    expect_resolved(&base, "../../../../g", "http://a/g");
    expect_resolved(&base, "/./g", "http://a/g");
    expect_resolved(&base, "/../g", "http://a/g");
    expect_resolved(&base, "g.", "http://a/b/c/g.");
    expect_resolved(&base, ".g", "http://a/b/c/.g");
    expect_resolved(&base, "g..", "http://a/b/c/g..");
    expect_resolved(&base, "..g", "http://a/b/c/..g");
    expect_resolved(&base, "./../g", "http://a/b/g");
    expect_resolved(&base, "./g/.", "http://a/b/c/g/");
    expect_resolved(&base, "g/./h", "http://a/b/c/g/h");
    expect_resolved(&base, "g/../h", "http://a/b/c/h");
    expect_resolved(&base, "g;x=1/./y", "http://a/b/c/g;x=1/y");
    expect_resolved(&base, "g;x=1/../y", "http://a/b/c/y");
    expect_resolved(&base, "g?y/./x", "http://a/b/c/g?y/./x");
    expect_resolved(&base, "g?y/../x", "http://a/b/c/g?y/../x");
    expect_resolved(&base, "g#s/./x", "http://a/b/c/g#s/./x");
    expect_resolved(&base, "g#s/../x", "http://a/b/c/g#s/../x");
    expect_resolved(&base, "http:g", "http:g");
}

@test
void normalizes_urls() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.url::url value = std.url::parse("HTTP://Example.COM:80/a/./b/../%7e%41%2f%c3%a9?q=%7E#%2a");
    std.url::url normal = value.normalized();
    std.string::string text = normal.text();
    std.test::equal_text(text, "http://example.com/a/~A%2F%C3%A9?q=~#%2A");
    std.url::url bare = std.url::parse("https://example.com:8443");
    std.url::url completed = bare.normalized();
    std.string::string shown = completed.text();
    std.test::equal_text(shown, "https://example.com:8443/");
}

@test
void encodes_and_decodes_percent_triplets() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.string::string encoded = std.url::percent_encode("a b/c?é~");
    std.test::equal_text(encoded, "a%20b%2Fc%3F%C3%A9~");
    std.string::string decoded = std.url::percent_decode(encoded);
    std.test::equal_text(decoded, "a b/c?é~");
    std.string::string plus = std.url::percent_decode("a+b");
    std.test::equal_text(plus, "a+b");
    try {
        std.string::string invalid = std.url::percent_decode("%ff");
        drop invalid;
        std.test::fail("invalid UTF-8");
    } catch (std.url::url_error failure) {
        std.test::check(failure.code == std.url::error_code::invalid_utf8, "invalid_utf8");
    }
    try {
        std.string::string cut = std.url::percent_decode("ab%4");
        drop cut;
        std.test::fail("cut triplet");
    } catch (std.url::url_error failure) {
        std.test::check(failure.code == std.url::error_code::invalid_percent_encoding, "triplet");
        std.test::equal(failure.offset, 2usize);
    }
}

@test
void reads_and_writes_form_queries() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    array<std.url::query_pair> pairs = std.url::parse_query("name=J%C3%BCrgen+M&empty=&flag&&a=b=c");
    std.test::equal(len(pairs), 4usize);
    std.test::equal_text(pairs[0usize].name, "name");
    std.test::equal_text(pairs[0usize].value, "Jürgen M");
    std.test::equal_text(pairs[1usize].name, "empty");
    std.test::equal_text(pairs[1usize].value, "");
    std.test::equal_text(pairs[2usize].name, "flag");
    std.test::equal_text(pairs[2usize].value, "");
    std.test::equal_text(pairs[3usize].value, "b=c");
    std.string::string query = std.string::create();
    std.url::append_query(&query, "q", "a b&c");
    std.url::append_query(&query, "lang", "ü");
    std.test::equal_text(query, "q=a+b%26c&lang=%C3%BC");
}

@test(allocations)
void parsing_reports_exhausted_memory() throws std.test::failure, std.url::url_error, std.alloc::alloc_error {
    std.url::url base = std.url::parse("http://a/b/c/d;p?q");
    std.url::url target = base.resolve("../g?x#y");
    std.url::url normal = target.normalized();
    std.string::string text = normal.text();
    std.test::equal_text(text, "http://a/b/g?x#y");
}
