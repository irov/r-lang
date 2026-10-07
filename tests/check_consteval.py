#!/usr/bin/env python3
"""Check translation-time evaluation across modules: interface metadata, determinism and
source-dependency fingerprints (Core R-FUNC-0023, R-EXPR-0032, R-MOD-0006)."""
import argparse
from pathlib import Path
import subprocess
import tempfile


API = '''module consteval.api;
struct Header { u32 magic; u16 length; };
struct Limits { u32 low; u32 high; };
const Limits LIMITS = Limits { .low = 10u32, .high = 250u32 };
const u32[3] STEPS = {1u32, 2u32, 3u32};
thread_local usize adjustment = 0;
usize frame_bytes(usize payload) { return sizeof(Header) + payload + 4usize; }
usize adjusted(usize value) { return value + adjustment; }
'''

APP = '''module consteval.app;
import consteval.api;
const usize FRAME = consteval.api::frame_bytes(16usize);
struct Frame { u8[FRAME] bytes; };
i32 main() {
    Frame frame = {};
    usize size = consteval.api::adjusted(len(frame.bytes));
    if (size != 28usize) {
        return 1;
    }
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-consteval-') as directory:
        root = Path(directory)
        api = root / 'api.r'
        app = root / 'app.r'
        api.write_text(API)
        app.write_text(APP)

        def emit(mode, paths):
            result = subprocess.run([args.front, '--emit=' + mode, *map(str, paths)],
                                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (mode, result.returncode, result.stderr)
            return result.stdout

        interface = emit('interface', [api, app])
        assert interface == emit('interface', [app, api])
        assert interface.startswith('(interface version=34 ')
        frame_bytes = next(line for line in interface.splitlines()
                           if '(function name="consteval.api::frame_bytes"' in line)
        adjusted = next(line for line in interface.splitlines()
                        if '(function name="consteval.api::adjusted"' in line)
        assert ' consteval=true' in frame_bytes, frame_bytes
        assert ' consteval=true' not in adjusted, adjusted
        dependencies = [line for line in interface.splitlines() if '(source-dependency' in line]
        assert any('module="consteval.api"' in line for line in dependencies), dependencies
        # R-MOD-0005: an exported aggregate const value is part of the interface.
        limits = next(line for line in interface.splitlines()
                      if '(object name="consteval.api::LIMITS"' in line)
        steps = next(line for line in interface.splitlines()
                     if '(object name="consteval.api::STEPS"' in line)
        assert 'value={10,250})' in limits, limits
        assert 'value={1,2,3})' in steps, steps
        api.write_text(API.replace('.high = 250u32', '.high = 251u32'))
        assert emit('interface', [api, app]) != interface
        api.write_text(API)
        generated = emit('c17', [api, app])
        assert generated == emit('c17', [app, api])
        assert 'r_data[28]' in generated, 'the module-scope bound was not computed'

        api.write_text(API.replace('+ 4usize', '+ 8usize'))
        changed = emit('interface', [api, app])
        assert changed != interface
        assert 'r_data[32]' in emit('c17', [api, app])

        api.write_text(API.replace('return sizeof(Header) + payload + 4usize;',
                                   'return sizeof(Header) + payload + adjustment;'))
        result = subprocess.run([args.front, '--emit=c17', str(api), str(app)],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode != 0 and 'R-DIAG-CONST-002' in result.stderr, result.stderr
        assert 'accesses an object with static or thread storage duration' in result.stderr
    print('consteval: interface evaluability, source dependencies and determinism passed')


if __name__ == '__main__':
    main()
