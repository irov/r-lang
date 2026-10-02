#!/usr/bin/env python3
"""Check the journal utility against actual temporary files and Python metadata."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, expected=None, status=0):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=15)
        assert result.returncode == status and not result.stderr, (args, result)
        if expected is not None:
            assert result.stdout == expected, (args, result.stdout, expected)
        count += 1
        return result.stdout

    def metadata(text, path, kind):
        lines = text.splitlines()
        actual = path.lstat()
        assert lines[0] == f'kind={kind} size={actual.st_size}'
        times = dict(line.split('=', 1) for line in lines[1:])
        for name, nanoseconds in [('modified', actual.st_mtime_ns), ('accessed', actual.st_atime_ns)]:
            seconds, nanos = map(int, times[name].split())
            assert seconds * 1000000000 + nanos == nanoseconds, (text, actual)
        assert times['created'] == 'unavailable' or len(times['created'].split()) == 2

    with tempfile.TemporaryDirectory(prefix='r-workspace-example-') as tmp:
        base = Path(tmp)
        root = base / 'journal'
        run('init', root, expected='created\n')
        run('init', root, status=74)
        run('first', root, expected='empty\n')
        for message in ['', 'hello', 'caf\u00e9 \U0001f642', 'x' * 10000]:
            data = b'\x01' + message.encode()
            run('record', root, 'note', message, expected=f'written={len(data)} size={len(data)}\n')
            assert (root / 'note').read_bytes() == data
            run('record', root, 'note', 'replacement', status=74)
            assert (root / 'note').read_bytes() == data
            os_stat = root / 'note'
            metadata(run('stat', root, 'note'), os_stat, 'regular')
            metadata(run('inspect', os_stat), os_stat, 'regular')
            run('first', root, expected='first=note\n')
            run('read', root, 'note', expected=f'bytes={len(data)} crc32={zlib.crc32(data)}\n')
            for size in [0, 1, 3, len(data) + 10]:
                length = min(size, len(data))
                suffix = data[len(data) - length:]
                run('tail', os_stat, size,
                    expected=f'offset={len(data)-length} bytes={length} crc32={zlib.crc32(suffix)}\n')
            run('publish', root, 'note', 'published', expected='published\n')
            run('publish', root, 'note', 'published', status=74)
            assert (root / 'published').read_bytes() == data
            run('publish', root, 'note', '../escape', status=64)
            assert not (base / 'escape').exists()
            run('rename', root, 'note', 'saved', expected='renamed\n')
            assert not (root / 'note').exists() and (root / 'saved').read_bytes() == data
            run('remove', root, 'saved', expected='removed\n')
            run('remove', root, 'published', expected='removed\n')
        run('mkdir', root, 'drafts', expected='empty\n')
        assert (root / 'drafts').is_dir()
        run('rmdir', root, 'drafts', expected='removed\n')
        run('first', root, expected='empty\n')
        run('stat', root, 'missing', status=74)
        run('read', root, '../outside', status=74)
        run('remove', root, '/absolute', status=64)
        run('tail', root / 'missing', 1, status=74)
        run('tail', root / 'missing', 1048577, status=64)
        run('unknown', root, status=64)
        run('record', root, status=64)
    print(f'Workspace: {count} journal, metadata, cursor, publication and directory checks passed')


if __name__ == '__main__':
    main()
