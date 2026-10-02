module test.codegen.async_user_drop;
struct Child { own i32* data; };
struct Parent { Child child; };
drop (Child* self) {

    *(self->data) = 9;
}
drop (Parent* self) {

    *(self->child.data) = 8;
}
error Failure { Parent payload; };

async i32 main() {
    Parent value = {.child=Child {.data=new i32(7)}};
    drop value;
    try {
        throw Failure {.payload=Parent {.child=Child {.data=new i32(7)}}};
    } catch (Failure caught) {
        if (*(caught.payload.child.data) != 7) { return 1; }
    }
    return 0;
}
