#!/usr/bin/env python3
"""Check imported static defaults, inherited bounds, dyn interfaces and their interface fingerprints."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-trait-contracts-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        consumer = root / 'consumer.r'
        definition = '''module traits.provider;
trait Source {
    type Item: copy;
    @noalloc @nonblocking Self::Item read(const Self* this);
    @noalloc @nonblocking Self::Item repeated(const Self* this) {
        Self::Item value=this->read();
        return value;
    }
};
trait Report: Source {};
@generic<T: copy>
trait Convert {
    T convert(const Self* this);
    T converted(const Self* this) { return this->convert(); }
};
'''
        provider.write_text(definition)
        consumer.write_text('''module traits.consumer;
import traits.provider;
struct Counter { i32 count; };
impl traits.provider::Source for Counter {
    type Item=i32;
    i32 read(const Counter* this) { return this->count; }
};
impl traits.provider::Report for Counter {};
impl traits.provider::Convert<i32> for Counter {
    i32 convert(const Counter* this) { return this->count; }
};
@generic<T: traits.provider::Convert<i32>>
i32 converted(const T* value) { return value->converted(); }
@generic<T: traits.provider::Report>
@noalloc @nonblocking
T::Item read_report(const T* value) { return value->repeated(); }
i32 main() { Counter value={.count=42}; return read_report(&value)+converted(&value)-84; }
''')

        def emit(mode, paths, accepted=True):
            result = subprocess.run([args.front, '--emit=' + mode, *map(str, paths)],
                                    text=True, capture_output=True, timeout=30)
            expected = 0 if accepted else 1
            assert result.returncode == expected, result.stderr
            return result.stdout if accepted else result.stderr

        interface = emit('interface', [provider, consumer])
        assert '(interface version=34 ' in interface
        assert 'module="traits.provider"' in interface
        assert 'method_contracts=' in interface
        assert '(name="Convert" module="traits.provider" arguments=(i32))' in interface
        assert 'default=true' in interface
        assert 'constraints=(copy)' in interface
        assert 'noalloc=true nonblocking=true' in interface
        assert interface == emit('interface', [consumer, provider])
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])

        # A changed default body changes the defining trait's fingerprint.
        provider.write_text(definition.replace('return value;', 'return move value;'))
        assert interface != emit('interface', [provider, consumer])
        provider.write_text(definition)

        # An unused implementation must meet inherited resource contracts.
        consumer.write_text('''module traits.consumer;
import traits.provider;
struct Counter {};
impl traits.provider::Source for Counter {
    type Item=i32;
    i32 read(const Counter* this) { own i32* value=new i32(42); return *value; }
};
''')
        assert 'R-DIAG-RESOURCE-001' in emit('interface', [provider, consumer], False)

        # R-TYPE-0051: an interface type is written by its canonical contract, and the tags of
        # its members do not depend on the order of sources.
        provider.write_text('''module traits.provider;
trait Store { u32 get(const Self* this); };
trait Named { u32 name(const Self* this); };
u32 total(const dyn(Store & Named)* first, const dyn(Named & Store)* second) {
    return first->get() + second->name();
}
''')
        consumer.write_text('''module traits.consumer;
import traits.provider;
struct Zeta { u32 v; };
struct Alpha { u32 v; };
impl traits.provider::Store for Zeta { u32 get(const Zeta* this) { return this->v; } };
impl traits.provider::Named for Zeta { u32 name(const Zeta* this) { return 26u32; } };
impl traits.provider::Store for Alpha { u32 get(const Alpha* this) { return this->v; } };
impl traits.provider::Named for Alpha { u32 name(const Alpha* this) { return 1u32; } };
i32 main() {
    Zeta zeta = {.v = 3u32};
    Alpha alpha = {.v = 4u32};
    u32 sum = traits.provider::total(&zeta, &alpha);
    i32 status = sum == 4u32 ? 0 : 1;
    return status;
}
''')
        interface = emit('interface', [provider, consumer])
        assert interface == emit('interface', [consumer, provider])
        assert '(const_borrow (dyn "dyn(traits.provider::Named & traits.provider::Store)"))' in \
            interface, interface
        program = emit('c17', [provider, consumer])
        assert program == emit('c17', [consumer, provider])
        assert 'typedef struct r_dyn {' in program

        # R-TYPE-0045 (L16): interface schema 22 records lending methods, the parameters of an
        # associated type and a binding with parameters, independently of the source order.
        provider.write_text('''module traits.provider;
trait Wrapper {
    type Of<T: copy>: copy;
    Self::Of<i32> wrap(Self* this, i32 value);
    i32 unwrap(Self* this, Self::Of<i32> value);
};
trait Lender {
    type View;
    @lending Self::View lend(Self* this);
};
@generic<W: Wrapper>
i32 roundtrip(W* wrapper, i32 value) {
    W::Of<i32> wrapped = wrapper->wrap(value);
    return wrapper->unwrap(wrapped);
}
''')
        consumer.write_text('''module traits.consumer;
import traits.provider;
struct Maybe { i32 fallback; };
impl traits.provider::Wrapper for Maybe {
    type Of<T> = o<T>;
    o<i32> wrap(Maybe* this, i32 value) { return o::some(value); }
    i32 unwrap(Maybe* this, o<i32> value) {
        switch (move value) {
        case variant o::some(move v): return v;
        case variant o::none: return this->fallback;
        }
    }
};
struct Cell { i32 value; };
impl traits.provider::Lender for Cell {
    type View = const i32*;
    const i32* lend(Cell* this) { return &this->value; }
};
i32 main() {
    Maybe maybe = {.fallback = 0};
    Cell cell = {.value = 2};
    const i32* view = cell.lend();
    i32 lent = *view;
    return traits.provider::roundtrip(&maybe, 40) + lent - 42;
}
''')
        interface = emit('interface', [provider, consumer])
        assert interface == emit('interface', [consumer, provider])
        assert 'lending=true' in interface, interface
        assert '(name="Of" parameters=((name="T" constraints=(copy))) constraints=(copy))' in \
            interface, interface
        assert 'bindings=((type_function parameters=("T") body=(option (parameter "T"))))' in \
            interface, interface
        assert '(application (parameter "Of") i32)' in interface, interface
        program = emit('c17', [provider, consumer])
        assert program == emit('c17', [consumer, provider])


if __name__ == '__main__':
    main()
