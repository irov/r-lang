#!/usr/bin/env python3
"""Check both keystore backends behind one interface against an independent dictionary model."""
import argparse
import random
import subprocess

CAPACITY = 8


def model(backend, commands):
    store = {}
    lines = []
    index = 0
    while index < len(commands):
        command = commands[index]
        if command == 'put':
            key, value = int(commands[index + 1]), int(commands[index + 2])
            index += 3
            if backend == 'slots' and key not in store and len(store) == CAPACITY:
                return ''.join(lines), f'store is full: capacity {CAPACITY}\n', 65
            store[key] = value
            lines.append(f'stored {key}\n')
        elif command == 'get':
            key = int(commands[index + 1])
            index += 2
            lines.append(f'{key}={store[key]}\n' if key in store else f'{key}=none\n')
        elif command == 'remove':
            key = int(commands[index + 1])
            index += 2
            lines.append(f'removed={"true" if store.pop(key, None) is not None else "false"}\n')
        elif command == 'count':
            index += 1
            lines.append(f'count={len(store)}\n')
        elif command == 'backend':
            index += 1
            lines.append(f'backend={backend}\n')
        else:
            raise AssertionError(command)
    return ''.join(lines), None, 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    executable = parser.parse_args().executable
    count = 0

    def run(arguments, expected, status=0):
        nonlocal count
        result = subprocess.run([executable, *map(str, arguments)], capture_output=True,
                                text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (arguments, result)
        assert result.stdout == expected, (arguments, result.stdout, expected)
        count += 1

    def check(backend, commands):
        output, failure, status = model(backend, commands)
        # A failed script prints only the diagnostic of its first error.
        run([backend, *commands], failure if failure is not None else output, status)

    for backend in ('slots', 'log'):
        check(backend, ['backend', 'count'])
        check(backend, ['put', 1, 10, 'put', 2, 20, 'get', 1, 'get', 3, 'count'])
        check(backend, ['put', 5, 1, 'put', 5, 2, 'get', 5, 'count', 'remove', 5, 'remove', 5,
                        'get', 5, 'count'])
        check(backend, [item for key in range(9) for item in ('put', key, key * 3)] + ['count'])
        generator = random.Random(backend)
        for _ in range(40):
            commands = []
            for _ in range(generator.randrange(1, 30)):
                choice = generator.randrange(4)
                key = generator.randrange(12)
                if choice == 0:
                    commands += ['put', key, generator.randrange(1000)]
                elif choice == 1:
                    commands += ['get', key]
                elif choice == 2:
                    commands += ['remove', key]
                else:
                    commands += ['count']
            check(backend, commands)
    run(['disk', 'count'], 'unknown keystore backend\n', 64)
    run(['slots', 'fetch', 1], 'unknown keystore command\n', 64)
    run(['log', 'put', 1], 'missing command operand\n', 64)
    print(f'Keystore: {count} backend and command checks passed')


if __name__ == '__main__':
    main()
