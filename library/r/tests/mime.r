module tests.std.mime;
import std.test;
import std.mime;

// The tests of std.mime (Library R-SLIB-MIME-0001..0002): media types with parameters, quoted
// values and escapes, rejections with their offsets, formatting and the media types of file
// extensions and paths. Run in test mode (Core R-FUNC-0025).

protected bool absent(o<str> value) {
    switch (value) {
    case variant o::some(found):
        found as void;
        return false;
    case variant o::none: return true;
    }
}

protected usize rejection(str text) throws std.test::failure, std.alloc::alloc_error {
    try {
        std.mime::media_type value = std.mime::parse(text);
        drop value;
    } catch (std.mime::mime_error failure) {
        return failure.offset;
    }
    std.test::fail("the text was accepted");
    return 0usize;
}

protected void expect_parameter(const std.mime::media_type* value, str name, str expected)
    throws std.test::failure, std.alloc::alloc_error {
    switch (value->parameter(name)) {
    case variant o::some(found): std.test::equal_text(*found, expected);
    case variant o::none: std.test::fail("missing parameter");
    }
}

@test
void parses_media_types() throws std.test::failure, std.mime::mime_error, std.alloc::alloc_error {
    std.mime::media_type html = std.mime::parse("Text/HTML ; Charset=UTF-8");
    std.test::equal_text(html.kind(), "text");
    std.test::equal_text(html.subtype(), "html");
    std.string::string essence = html.essence();
    std.test::equal_text(essence.as_str(), "text/html");
    expect_parameter(&html, "charset", "UTF-8");
    expect_parameter(&html, "CHARSET", "UTF-8");
    std.test::check(absent(html.parameter("boundary")) == true, "no boundary");
    std.mime::media_type form =
        std.mime::parse("multipart/form-data; boundary=\"a b\\\"c\"; x=1;");
    std.test::equal(form.parameter_count(), 2usize);
    expect_parameter(&form, "boundary", "a b\"c");
    std.string::string shown = f"{form}";
    std.test::equal_text(shown.as_str(), "multipart/form-data; boundary=\"a b\\\"c\"; x=1");
    std.mime::media_type json = std.mime::parse("application/vnd.api+json");
    std.string::string plain = f"{json}";
    std.test::equal_text(plain.as_str(), "application/vnd.api+json");
}

@test
void rejects_invalid_media_types() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(rejection(""), 0usize);
    std.test::equal(rejection("text"), 4usize);
    std.test::equal(rejection("text/"), 5usize);
    std.test::equal(rejection("te xt/plain"), 2usize);
    std.test::equal(rejection("text/plain; charset"), 19usize);
    std.test::equal(rejection("text/plain; =x"), 12usize);
    std.test::equal(rejection("text/plain; a=\"open"), 14usize);
    std.test::equal(rejection("text/plain x"), 11usize);
    std.test::equal(rejection("text/plain; a="), 14usize);
}

@test
void finds_media_types_of_extensions() throws std.test::failure, std.alloc::alloc_error {
    switch (std.mime::from_extension("JSON")) {
    case variant o::some(found): std.test::equal_text(*found, "application/json");
    case variant o::none: std.test::fail("json");
    }
    std.test::check(absent(std.mime::from_extension("exe")) == true, "unknown");
    std.test::equal_text(std.mime::for_path("/static/app.min.js"), "text/javascript; charset=utf-8");
    std.test::equal_text(std.mime::for_path("index.HTML"), "text/html; charset=utf-8");
    std.test::equal_text(std.mime::for_path("/a.dir/README"), "application/octet-stream");
    std.test::equal_text(std.mime::for_path("archive."), "application/octet-stream");
    std.test::check(std.mime::is_token("gzip") == true, "a token");
    std.test::check(std.mime::is_token("a b") == false, "not a token");
}

@test(allocations)
void parsing_reports_exhausted_memory() throws std.test::failure, std.mime::mime_error, std.alloc::alloc_error {
    std.mime::media_type value = std.mime::parse("text/plain; charset=utf-8; format=flowed");
    std.string::string shown = f"{value}";
    std.test::equal_text(shown.as_str(), "text/plain; charset=utf-8; format=flowed");
}
