module test.aggregate.api;

enum state {
    idle,
    ready,
};

struct point {
    i32 x;
    i32 y;
};

protected struct secret {
    i32 value;
};

struct guarded {
    protected i32 hidden;
    i32 shown;
};
