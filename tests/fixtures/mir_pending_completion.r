module test.mir_pending_completion;

error pending_error {
    i32 code;
};

void normal() {
    try {
    } finally {
    }
}

i32 returning() {
    try {
        return 7;
    } finally {
    }
}

void propagating() throws pending_error {
    try {
        throw {.code = 1};
    } finally {
    }
}

void loops(bool repeat) {
    while (true) {
        try {
            if (repeat == true) {
                continue;
            } else {
                break;
            }
        } finally {
        }
    }
}

void catching() {
    try {
        try {
            throw pending_error {.code = 2};
        } finally {
        }
    } catch (pending_error error) {
        error.code as void;
    }
}

async void cancelling(task<void> operation) {
    try {
        await move operation;
    } finally {
    }
}

void nested() {
    try {
        try {
            return;
        } finally {
        }
    } finally {
    }
}
