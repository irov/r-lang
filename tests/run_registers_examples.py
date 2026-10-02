#!/usr/bin/env python3
"""Compare register scripts with integer arithmetic and race native ticket issuers."""
import argparse
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0
    mask = 2**32 - 1

    def run(*args, status=0, expected=None):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=10)
        assert result.returncode == status and not result.stderr, (args[:3], result.returncode, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args[:3], result.stdout, expected)
        count += 1
        return result.stdout

    def script(initial, operations):
        value = initial
        args = []
        expected = ''
        for operation, operand, *extra in operations:
            args += [operation, operand, *extra]
            before = value
            accepted = True
            if operation == 'add': value = (value + operand) & mask
            elif operation == 'sub': value = (value - operand) & mask
            elif operation == 'bit_and': value &= operand
            elif operation == 'bit_or': value |= operand
            elif operation == 'bit_xor': value ^= operand
            elif operation == 'exchange': value = operand
            elif operation == 'cas':
                accepted = value == operand
                if accepted: value = extra[0]
            expected += f'{operation} before={before} after={value} accepted={str(accepted).lower()}\n'
        output = run('script', initial, *args)
        assert output in [expected + f'value={value} lock_free={flag}\n' for flag in ['true', 'false']], output

    script(0, [])
    script(mask, [('add', 1), ('sub', 1), ('cas', mask, 7), ('cas', mask, 9),
                  ('bit_and', 3), ('bit_or', 8), ('bit_xor', 5), ('exchange', 2)])
    rng = random.Random(264)
    for _ in range(24):
        operations = []
        for _ in range(35):
            name = rng.choice(['add', 'sub', 'bit_and', 'bit_or', 'bit_xor', 'exchange', 'cas'])
            operands = [rng.randrange(2**32)]
            if name == 'cas': operands.append(rng.randrange(2**32))
            operations.append((name, *operands))
        script(rng.randrange(2**32), operations)
    for iterations in [0, 1, 100, 10000, 100000] * 3:
        tickets = iterations * 2
        run('race', iterations, expected=f'tickets={tickets} sum={tickets*(tickets-1)//2}\n')
    for initial, writes in [(0, []), (0, [1, mask, 0]), (mask, [5, 6, 5, 5])]:
        expected = ''
        previous = initial
        for value in writes:
            expected += f'before={previous} after={value}\n'
            previous = value
        run('device', initial, *writes, expected=expected)
    for args in [('script',), ('script', 0, 'add'), ('script', 0, 'cas', 0),
                 ('script', 0, 'bad', 1), ('race', 100001), ('race', 1, 2), ('bad', 1)]:
        run(*args, status=64)
    for args in [('script', -1), ('script', 0, 'add', 2**32), ('device', 0, 'bad')]:
        run(*args, status=65)
    print(f'Registers: {count} script, CAS, wrapping, ticket race, volatile and validation checks passed')


if __name__ == '__main__':
    main()
