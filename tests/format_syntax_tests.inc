static void r_test_format_syntax(void) {
    static const struct {
        const char *body;
        bool valid;
    } cases[] = {
        {"std.string::string s = f\"hello {name}\";", true},
        {"std.format::format f0 = f\"{name} {1} {2} {1}\";", true},
        {"std.string::string s = f\"{1:08X}\".format((count + 1));", true},
        {"std.string::string s = \"{ordinary}\" /* join */ f\"{entry.field}/{ptr->field}\";", true},
        {"std.string::string s = f\"{{{имя}}}/\\u{7b}/\\x7d\"; i32 f = 1;", true},
        {"std.string::string s = \"\\xC3\" f\"\\xA9\";", true},
        {"std.string::string s = f\"{name\";", false},
        {"std.string::string s = f\"{}\";", false},
        {"std.string::string s = f\"{name + 1}\";", false},
        {"std.string::string s = f\"{name()}\";", false},
        {"std.string::string s = f\"{name[0]}\";", false},
        {"std.string::string s = f\"{1.field}\";", false},
        {"std.string::string s = f\"unmatched }\";", false},
        {"std.string::string s = f\"{1}\".format( ;", false},
        {"std.string::string s = f\"\\xC3{name}\\xA9\";", false},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char text[1024];
        int length = snprintf(text,
                              sizeof(text),
                              "module syntax.format; void test() { %s } i32 after() { return 0; }",
                              cases[index].body);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RTestBuffer reconstructed = {0};
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        RSourceId source =
            r_test_add_source(context, "format.r", (const uint8_t *)text, (size_t)length);
        RFrontendStatus status = r_frontend_parse_cst(context, source, &cst);
        R_TEST_CHECK(status == (cases[index].valid ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE));
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}
