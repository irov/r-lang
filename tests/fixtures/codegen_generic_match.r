module test.codegen.generic_match;
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
@generic<T>
enum Box { Some(T) };
@generic<T>
T unpack(Box<T> box) {
    return match (move box) { case variant Box<T>::Some(move value): move value; };
}
struct Views { const i32* left; const i32* right; };
const i32* select(Views input) {
    return match(input) { case {.right=selected}: selected; };
}
Views make_views(const i32* a, const i32* b) { return Views{.left=a,.right=b}; }
const i32* select_temporary(const i32* a, const i32* b) {
    return match(make_views(a,b)) { case {.right=p}: p; };
}
enum Borrowed { Some(const i32*) };
const i32* extract(const i32* p) {
    return match(Borrowed::Some(p)) { case variant Borrowed::Some(view): view; };
}
trait Inspect { i32 read(const Self* this); };
impl Inspect for Views { i32 read(const Views* this) { return *(this->right); } };
@generic<T: Inspect>
i32 inspect_box(Box<T> box) {
    return match(move box) { case variant Box<T>::Some(view): view.read(); };
}
opaque(Inspect) hide(Views view) { return view; }
@generic<T: Inspect>
i32 inspect_value(T value) {
    Box<T> box=Box<T>::Some(move value);
    return inspect_box(move box);
}
i32 next(i32* sequence) { *sequence += 1; return *sequence; }
i32 main() {
    try {
        i32 sequence=0;
        auto number=match(next(&sequence)) {
            case 0: panic("unexpected input");
            case 1: 42;
            default: panic("evaluated twice");
        };
        Box<i32> copy=Box<i32>::Some(number);
        if (unpack(move copy) != 42 || sequence != 1) { throw TestAssertionFailed {.code = 1}; }
        Owner a={.value=new i32(20)};
        Owner b={.value=new i32(22)};
        Box<Owner> first=Box<Owner>::Some(move a);
        Box<Owner> second=Box<Owner>::Some(move b);
        Owner left=unpack(move first);
        Owner right=unpack(move second);
        Views views={.left=&*(left.value),.right=&*(right.value)};
        const i32* remaining=select(views);
        const i32* selected=select_temporary(&*(left.value), &*(right.value));
        selected as void;
        const i32* nested=extract(&*(right.value));
        nested as void;
        if (inspect_box(Box<Views>::Some(views)) != 22 || inspect_value(hide(views)) != 22) { throw TestAssertionFailed {.code = 3}; }
        drop left;
        i32 chosen = *remaining == 22 && *selected == 22 && *nested == 22 ? 0 : 2;
        return chosen;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
