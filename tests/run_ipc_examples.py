#!/usr/bin/env python3
"""Exercise the Unix-domain line service, datagrams, peer credentials and signals of ipc."""
import argparse
import os
import signal
import subprocess
import tempfile
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    exe = os.path.abspath(parser.parse_args().executable)
    count = 0

    with tempfile.TemporaryDirectory() as root:
        # Relative socket paths keep every path far below the 103-byte limit.
        def run(*args, status=0, expected=None, contains=None):
            nonlocal count
            result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True,
                                    timeout=20, cwd=root)
            assert result.returncode == status and not result.stderr, (args, result.returncode, result.stdout, result.stderr)
            if expected is not None:
                assert result.stdout == expected, (args, result.stdout, expected)
            if contains is not None:
                assert contains in result.stdout, (args, result.stdout, contains)
            count += 1
            return result.stdout

        def start(*args):
            process = subprocess.Popen([exe, *map(str, args)], stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, cwd=root)
            ready = process.stdout.readline()
            assert ready == f'{args[0].replace("serve", "listening").replace("collect", "collecting")} {args[1]}\n', ready
            return process

        def finish(process, expected, status=0):
            nonlocal count
            output, errors = process.communicate(timeout=20)
            assert process.returncode == status and not errors, (process.returncode, output, errors)
            assert output == expected, (output, expected)
            count += 1

        help_text = run()
        assert help_text.startswith('ipc serve PATH LIMIT\n'), help_text
        for args in [('bogus', 'a', 'b'), ('serve', 'a'), ('signal', '0'), ('signal', '17'),
                     ('collect', 'a', '1', '70000'), ('serve', 'a', '0'), ('call', 'a', 'x' * 4001)]:
            run(*args, status=64)
        run('serve', 'a', 'many', status=65)

        # Deliveries raised before the wait may merge into one completion.
        for signals in (1, 3):
            run('signal', signals, expected=f'user1 raised={signals} delivered_within_range=true\n')

        # A line service that answers with the user identifier the socket recorded for its client.
        messages = ['hello', 'café \U0001f642', '']
        server = start('serve', 'lines.sock', len(messages))
        for message in messages:
            length = len(message.encode())
            run('call', 'lines.sock', message,
                expected=f'uid={os.getuid()} process_known=true length={length} {message}\n')
        finish(server, f'served {len(messages)}\n')
        run('call', 'lines.sock', 'late', status=69, contains='connection_refused')

        # SIGTERM and SIGINT stop the service between clients; a stale socket file is replaced.
        for stop in (signal.SIGTERM, signal.SIGINT):
            server = start('serve', 'lines.sock', 10)
            run('call', 'lines.sock', 'one', expected=f'uid={os.getuid()} process_known=true length=3 one\n')
            run('notify', 'lines.sock', 'wrong kind', status=69, contains='unsupported')
            server.send_signal(stop)
            finish(server, 'served 1\n')

        # Datagrams from unnamed sockets: a cut prefix reports truncated, an empty one is sent too.
        collector = start('collect', 'events.sock', 3, 5)
        for message in ['hello', 'hello world', '']:
            run('notify', 'events.sock', message, expected=f'sent {len(message)}\n')
        lines = ''.join(f'count={min(len(message), 5)} truncated={str(len(message) > 5).lower()} '
                        f'crc32={zlib.crc32(message[:5].encode())}\n'
                        for message in ['hello', 'hello world', ''])
        finish(collector, lines)

        run('call', 'missing.sock', 'x', status=69, contains='address_not_available')
        run('notify', 'missing.sock', 'x', status=69, contains='address_not_available')
        run('call', 'p' * 120, 'x', status=69, contains='invalid_address')
    print(f'Local IPC: {count} Unix-domain stream, datagram, credential, signal and validation checks passed')


if __name__ == '__main__':
    main()
