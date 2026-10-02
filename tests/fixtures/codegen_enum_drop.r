module test.codegen.enum_drop;
struct Child { own i32* data; };
struct Parent { Child child; };
drop (Child* self) { *(self->data) = 9; }
drop (Parent* self) { *(self->child.data) = 8; }
error Fault { Missing, Data(Parent), Fields { Parent item; }, Closed, };
drop (Fault* self) {
    switch (*self) {
    case variant Fault::Missing: break;
    case variant Fault::Data(value): break;
    case variant Fault::Fields(value): break;
    case variant Fault::Closed: break;
    }
    own i32* marker = new i32(99);
    drop marker;
}
protected Fault make() {
    return Fault::Data(Parent {.child=Child {.data=new i32(7)}});
}
protected void raise(bool fields) throws Fault {
    throw (fields == true) Fault::Fields {.item=Parent {.child=Child {.data=new i32(7)}}}
        else Fault::Data(Parent {.child=Child {.data=new i32(7)}});
}
i32 main() {
    Parent initial = {.child=Child {.data=new i32(7)}};
    drop initial;
    i32 finally_count = 0;
    try {
        try { raise(false); } catch (Fault caught) { throw; }
        return 1;
    } catch (Fault caught) {
        switch(caught) {
        case variant Fault::Data(value):
            if (*((*value).child.data) != 7) { return 2; }
            break;
        default: return 3;
        }
    } finally { finally_count += 1; }
    Fault fields = Fault::Fields {.item=Parent {.child=Child {.data=new i32(7)}}};
    drop fields;
    Fault missing = Fault::Missing;
    drop missing;
    if (finally_count != 1) { return 4; }
    return 0;
}
