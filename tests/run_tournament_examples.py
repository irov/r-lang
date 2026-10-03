#!/usr/bin/env python3
"""Compare tournament schedules, registration and priority reports with Python."""
import argparse
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = parser.parse_args().executable
    count = 0

    def run(*args, status=0, expected=None):
        nonlocal count
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=15)
        assert result.returncode == status and not result.stderr, (args[:4], result.returncode, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args[:4], result.stdout, expected)
        count += 1

    for players in [2, 3, 4, 8, 17, 32]:
        expected = ''.join(f'{a}-{b}\n' for a in range(1, players+1) for b in range(a+1, players+1))
        expected += f'matches={players*(players-1)//2} scoring_rules=3\n'
        expected += ''.join(f'player={a} matches={players-1}\n' for a in range(1, players+1))
        run('schedule', players, expected=expected)
    for players in [0, 1, 8, 32]:
        for wanted in [0, 1, players, players+1]:
            run('draw', players, wanted, expected=f'player={wanted} registered={str(1 <= wanted <= players).lower()}\n')
    rng = random.Random(883)
    for scores in [[], [0], [10, 4, 10, -1], [-1000000, 1000000]] + [
            [rng.randint(-1000, 1000) for _ in range(50)] for _ in range(20)]:
        bonus = rng.randint(-1000000, 1000000)
        expected = ''.join(f'score={s} adjusted={s+bonus}\n' for s in sorted(scores, reverse=True))
        expected += f'count={len(scores)} total={sum(scores)}\nremaining=0 capacity_retained=true\n'
        run('rank', bonus, *scores, expected=expected)
    # Circle method: the first seat stays, the rest rotate right one seat per round.
    for players in [2, 3, 4, 5, 8, 9, 32]:
        for first in sorted({1, 2, players}):
            seats = list(range(1, players + 1)) + ([0] if players % 2 else [])
            seats = seats[first - 1:] + seats[:first - 1]
            size = len(seats)
            lines = []
            met = set()
            hosted = [0] * (players + 1)
            for round_number in range(1, size):
                pairs = []
                for seat in range(size // 2):
                    home, away = seats[seat], seats[size - 1 - seat]
                    if seat == 0 and round_number % 2 == 0:
                        home, away = away, home
                    if home and away:
                        pairs.append(f' {home}-{away}')
                        met.add(frozenset((home, away)))
                        hosted[home] += 1
                lines.append(f'round={round_number}' + ''.join(pairs) + '\n')
                seats = [seats[0], seats[-1]] + seats[1:-1]
            assert len(met) == players * (players - 1) // 2
            home = f'{min(hosted[1:])}-{max(hosted[1:])}'
            lines.append(f'pairings={len(met)} repeated=0 highest={players - 1}-{players} home={home}\n')
            run('rounds', players, *([first] if first != 1 else []), expected=''.join(lines))
    rng2 = random.Random(1726)
    for size in [1, 2, 5, 12]:
        rows = [(f'p{index}', rng2.randint(-5, 5)) for index in range(size)]
        ordered = sorted(enumerate(rows), key=lambda item: (-item[1][1], item[1][0].encode()))
        expected = ''.join(f'{place}. {name} {points} (entry {entry + 1})\n'
                           for place, (entry, (name, points)) in enumerate(ordered, start=1))
        expected += f'entries={size}\n'
        run('table', *[f'{name}:{points}' for name, points in rows], expected=expected)
    for args in [('rounds', 1), ('rounds', 33), ('rounds', 4, 0), ('rounds', 4, 5), ('rounds', 4, 1, 1),
                 ('table',), ('table', 'nameless'), ('table', ':3')]:
        run(*args, status=64)
    for args in [('rounds', 'x'), ('table', 'ann:x')]:
        run(*args, status=65)
    for args in [('schedule', 0), ('schedule', 1), ('schedule', 33), ('draw', 33, 1),
                 ('rank', 1000001), ('rank', 0, -1000001), ('schedule',), ('draw', 1), ('unknown',)]:
        run(*args, status=64)
    for args in [('schedule', 'bad'), ('draw', '-1', '0'), ('rank', '0', 'bad')]:
        run(*args, status=65)
    print(f'Tournament: {count} schedule, registration, ranking, round, table and validation checks passed')


if __name__ == '__main__':
    main()
