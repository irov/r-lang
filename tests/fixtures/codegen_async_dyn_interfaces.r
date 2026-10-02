module test.codegen.async_dyn_interfaces;

/* R-TYPE-0051: a scoped async method dispatched through an interface inside a task group; each
   start selects the frame of the member's implementation. */

async void pause() {}

trait Store : sync {
    @scoped
    async u32 load(const Self* this, u32 key) throws std.async::start_error;
    u32 size(const Self* this);
};

struct Memory { u32 base; };
struct Remote { u32 factor; };

impl Store for Memory {
    @scoped
    async u32 load(const Memory* this, u32 key) throws std.async::start_error {
        await pause();
        return this->base + key;
    }
    u32 size(const Memory* this) { return 1u32; }
};
impl Store for Remote {
    @scoped
    async u32 load(const Remote* this, u32 key) throws std.async::start_error {
        return this->factor * key;
    }
    u32 size(const Remote* this) { return 2u32; }
};

async i32 main() {
    try {
        Memory memory = {.base = 10u32};
        Remote remote = {.factor = 3u32};
        const dyn(Store & sync)* first = &memory;
        const dyn(Store & sync)* second = &remote;
        u32 sizes = first->size() + second->size();
        task_scope(2) group {
            auto a = first->load(5u32);
            auto b = second->load(5u32);
            u32 x = await move a;
            u32 y = await move b;
            if (x + y + sizes != 33u32) {
                return 1;
            }
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}
