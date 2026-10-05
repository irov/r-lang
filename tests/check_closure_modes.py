#!/usr/bin/env python3
"""Check callable modes, source-module inference and stable interface identities."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-closure-modes-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        consumer = root / 'consumer.r'
        provider.write_text('''module closure.provider;
@generic<F: fn once() -> i32 & send & unborrowed>
i32 consume(F operation) { i32 value = (move operation).call(); return value; }
@generic<F: fn mut(i32) -> i32>
i32 update(F* operation, i32 value) { i32 result = operation(value); return result; }
@generic<F: fn shared() -> i32>
i32 read(const F* operation) { i32 result = operation(); return result; }
error Failure { i32 code; };
@generic<F: fn @noalloc once() -> i32 throws(Failure)>
@noalloc i32 checked(F operation) throws Failure { i32 result = (move operation).call(); return result; }
@generic<F: async fn(i32) -> i32 & send & unborrowed>
task<i32> launch(F operation, i32 value) throws std.async::start_error {
    task<i32> pending = (move operation).call(value); return move pending;
}
@generic<T>
T relay(T value) {
    fn once T identity(T argument) { return move argument; }
    T result = identity(move value); return move result;
}
''')
        consumer.write_text('''module closure.consumer;
import closure.provider;
async i32 main() {
    own i32* value = new i32(42);
    fn once i32 work() move(value) { return *value; }
    i32 original = closure.provider::relay(42);
    async fn i32 later(i32 argument) { return argument; }
    fn @noalloc once i32 checked() throws closure.provider::Failure { return 0; }
    try {
        i32 received = await closure.provider::launch(later, original);
        i32 status = closure.provider::checked(checked); status as void;
        if (received != 42 || status != 0) { return 9; }
    } catch (std.async::start_error failure) { return 8; }
      catch (closure.provider::Failure failure) { return 7; }
    i32 total = 0;
    fn mut i32 accumulate(i32 delta) move(total) { total += delta; return total; }
    fn shared i32 snapshot() move(total) { return total; }
    i32 initial = closure.provider::read(&snapshot);
    i32 delta = closure.provider::update(&accumulate, 42);
    try {
        std.thread::join_handle<i32> child = std.thread::spawn(closure.provider::consume, move work);
        std.thread::join_result<i32> completion = (move child).join();
        switch (move completion) {
        case variant std.thread::join_result::returned(move result): return result + initial - delta;
        case variant std.thread::join_result::panicked(move report): initial as void; delta as void; return 1;
        }
    } catch (std.thread::thread_error failure) { initial as void; delta as void; return 2; }
}
''')

        def emit(mode, paths):
            result = subprocess.run([args.front, '--emit=' + mode, *map(str, paths)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (result.returncode, result.stderr)
            return result.stdout

        interface = emit('interface', [provider, consumer])
        assert interface == emit('interface', [consumer, provider])
        assert interface.endswith(')\n')
        assert '(interface version=32 ' in interface
        for mode in ('shared', 'mut', 'once'):
            assert '(callable mode=' + mode in interface
        assert 'async=true' in interface
        assert 'noalloc=true' in interface and ' throws=' in interface
        assert '$callable' not in interface
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])
        # The explicit shared spelling and omitted shared mode have the same type contract.
        provider.write_text(provider.read_text().replace('fn shared()', 'fn()'))
        omitted = emit('interface', [provider, consumer])
        assert '(callable mode=shared parameters=() return=i32)' in omitted
    print('closure modes: module inference, structural signatures and deterministic output passed')


if __name__ == '__main__':
    main()
