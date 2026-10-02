module audit.switch_break_escaping_finally;

void invalid(bool leave) {
    switch (0) {
        case 0:
            try {
                leave as void;
            } finally {
                if (leave == true) {
                    break;
                }
            }
            break;
        default:
            break;
    }
}

i32 main() {
    return 0;
}
