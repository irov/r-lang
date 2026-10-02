#!/usr/bin/env python3
"""Run the loopback TCP service in every mode and check the replies, the service account and the
records of std.log; in repeat mode, check that a budget (Core R-STMT-0020) refuses a build beyond
it while the handlers that fit it complete; check the command line of std.args and the layered settings of std.config;
in serve mode, stop the service with SIGTERM or SIGINT while a connection is still being read
and check that the drain lets that handler finish."""
import argparse
import json
import os
import re
import signal
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()

HELP = ('service - a TCP service with a bounded handler group\n'
        'usage: service [options] [mode] [request...]\n'
        '  -c, --config FILE        JSON settings file\n'
        '  --capacity N             handlers at once\n'
        '  -t, --timeout_ms MS      connection timeout in milliseconds\n'
        '  --log.level LEVEL        lowest level of the records\n'
        '  --log.format FORMAT      text or json records\n'
        '  --log.file FILE          append the records to FILE\n'
        '  --budget_bytes N         bytes a repeat handler may hold\n'
        '  -v, --verbose            records from level debug\n'
        '  mode                     echo, count, watch, repeat, serve or show\n'
        '  request                  requests to send\n'
        '  -h, --help               print this help\n')
TEXT_RECORD = re.compile(
    r'^time=(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z) level=(\w+) task=(\d+) (message=.*)$')
# The service must not see settings of the environment that runs the test.
ENVIRONMENT = {name: value for name, value in os.environ.items()
               if not name.startswith('SERVICE_')}


def records(text):
    """The text records without their time and task, after checking that the time is RFC 3339
    with milliseconds and that the task identifier is at least one."""
    result = []
    for line in text.splitlines():
        match = TEXT_RECORD.match(line)
        assert match, line
        assert int(match.group(3)) >= 1, line
        result.append(f'{match.group(2)} {match.group(4)}')
    return result


def starting(mode, requests, capacity=64, timeout=1000):
    return (f'info message="service starting" mode={mode} capacity={capacity} '
            f'timeout_ms={timeout} requests={requests}')


def stopped(level, accepted, completed, failed, last=None):
    line = (f'{level} message="service stopped" accepted={accepted} rejected=0 '
            f'completed={completed} failed={failed} cancelled=0')
    return line if last is None else f'{line} last_failure={last}'


def run(values, environment=None):
    return subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=60,
                          env={**ENVIRONMENT, **(environment or {})})


cases = [([], HELP, 0, []),
         (['-h'], HELP, 0, []),
         (['echo', '--help'], HELP, 0, []),
         (['echo'], 'echo, count, watch and repeat need requests\n' + HELP, 64, []),
         (['ping', 'x'], 'unknown mode\n' + HELP, 64, []),
         (['serve', 'x'], 'serve takes no requests\n' + HELP, 64, []),
         (['--colour', 'echo', 'x'], 'unknown_option at argument 1\n' + HELP, 64, []),
         (['echo', '--timeout_ms'], 'missing_value at argument 2\n' + HELP, 64, []),
         (['echo', 'hello', 'world'],
          'hello -> hello\nworld -> world\n'
          'accepted=2 rejected=0 completed=2 failed=0 cancelled=0\n', 0,
          [starting('echo', 2), stopped('info', 2, 2, 0)]),
         (['count', '5', '7', 'x', 'hold', '11'],
          '5 -> 5\n7 -> 12\nx -> no reply\nhold -> no reply\n11 -> 23\n'
          'accepted=5 rejected=0 completed=3 failed=2 cancelled=0 last_failure=timed_out\n'
          'audit additions=3 sum=23\n', 0,
          [starting('count', 5), stopped('warn', 5, 3, 2, 'timed_out')]),
         (['-v', 'count', '40', 'x'],
          '40 -> 40\nx -> no reply\n'
          'accepted=2 rejected=0 completed=1 failed=1 cancelled=0 last_failure=invalid_digit\n'
          'audit additions=1 sum=40\n', 0,
          [starting('count', 2), 'debug message="amount added" amount=40 total=40',
           stopped('warn', 2, 1, 1, 'invalid_digit')]),
         (['watch', '5', '7', 'status', 'limit=6', '9', '4', 'status'],
          '5 -> 5\n7 -> 12\nstatus -> total=12 limit=100 free=2\nlimit=6 -> was 100\n'
          '9 -> refused above 6\n4 -> 16\nstatus -> total=16 limit=6 free=2\n'
          'accepted=7 rejected=0 completed=7 failed=0 cancelled=0\n'
          'watch lagged=1 12 16\nbooks total=16 limit=6 free=2\n', 0,
          [starting('watch', 7), stopped('info', 7, 7, 0)]),
         (['repeat', '1000', '100000', '50', 'x'],
          '1000 -> 1000 bytes in 16 lines\n100000 -> refused budget_exhausted\n'
          '50 -> 50 bytes in 1 lines\nx -> no reply\n'
          'accepted=4 rejected=0 completed=3 failed=1 cancelled=0 last_failure=invalid_digit\n', 0,
          [starting('repeat', 4), stopped('warn', 4, 3, 1, 'invalid_digit')]),
         (['--budget_bytes', '200000', 'repeat', '60000'],
          '60000 -> 60000 bytes in 938 lines\n'
          'accepted=1 rejected=0 completed=1 failed=0 cancelled=0\n', 0,
          [starting('repeat', 1), stopped('info', 1, 1, 0)]),
         (['watch', 'x'],
          'x -> no reply\n'
          'accepted=1 rejected=0 completed=0 failed=1 cancelled=0 last_failure=invalid_digit\n'
          'watch\nbooks total=0 limit=100 free=2\n', 0,
          [starting('watch', 1), stopped('warn', 1, 0, 1, 'invalid_digit')]),
         (['--log.level', 'warn', '--capacity', '2', 'echo', 'a'],
          'a -> a\naccepted=1 rejected=0 completed=1 failed=0 cancelled=0\n', 0, []),
         (['--log.level', 'loud', 'echo', 'a'], 'log.level names no level\n', 78, []),
         (['--log.format', 'xml', 'echo', 'a'], 'log.format is neither text nor json\n', 78, []),
         (['--capacity', 'many', 'echo', 'a'], 'invalid configuration: invalid_value at 0\n', 78,
          []),
         (['--capacity', '5000', 'echo', 'a'], 'capacity is 1 to std.service::max_capacity\n', 78,
          [])]
for values, output, status, expected in cases:
    result = run(values)
    assert (result.returncode, result.stdout) == (status, output), (values, result)
    assert records(result.stderr) == expected, (values, result.stderr)

# JSON records keep their members in order; the task and the numbers are not quoted.
result = run(['--log.format', 'json', '-t', '500', 'echo', 'a'])
assert (result.returncode, result.stdout) == (
    0, 'a -> a\naccepted=1 rejected=0 completed=1 failed=0 cancelled=0\n'), result
lines = [json.loads(line) for line in result.stderr.splitlines()]
assert [list(line) for line in lines] == [
    ['time', 'level', 'task', 'message', 'mode', 'capacity', 'timeout_ms', 'requests'],
    ['time', 'level', 'task', 'message', 'accepted', 'rejected', 'completed', 'failed',
     'cancelled']], lines
assert all(isinstance(line['task'], int) and line['task'] >= 1 for line in lines), lines
assert [{name: value for name, value in line.items() if name not in ('time', 'task')}
        for line in lines] == [
    {'level': 'info', 'message': 'service starting', 'mode': 'echo', 'capacity': 64,
     'timeout_ms': 500, 'requests': 1},
    {'level': 'info', 'message': 'service stopped', 'accepted': 1, 'rejected': 0,
     'completed': 1, 'failed': 0, 'cancelled': 0}], lines

with tempfile.TemporaryDirectory() as directory:
    # log.file appends the records to the file and leaves standard error empty.
    journal = os.path.join(directory, 'service.log')
    for _ in range(2):
        result = run(['--log.file', journal, 'echo', 'a'])
        assert (result.returncode, result.stderr) == (0, ''), result
    with open(journal, encoding='utf-8') as source:
        assert records(source.read()) == [starting('echo', 1), stopped('info', 1, 1, 0)] * 2
    # Layers: defaults, the file of --config, SERVICE_* variables, then the options.
    settings = os.path.join(directory, 'service.json')
    with open(settings, 'w', encoding='utf-8') as target:
        json.dump({'capacity': 16, 'timeout_ms': 2000,
                   'log': {'level': 'warn', 'file': None}}, target)
    result = run(['--timeout_ms', '250', '-c', settings, 'show'],
                 {'SERVICE_CAPACITY': '8', 'SERVICE_LOG_FORMAT': 'json'})
    assert (result.returncode, result.stderr) == (0, ''), result
    assert result.stdout == ('capacity=8 (environment)\ntimeout_ms=250 (arguments)\n'
                             'log.level=warn (file)\nlog.format=json (environment)\n'
                             'log.file= (default_value)\nbudget_bytes=4096 (default_value)\n'), result
    result = run(['-v', '--config', settings, 'show'])
    assert result.stdout.splitlines()[:3] == [
        'capacity=16 (file)', 'timeout_ms=2000 (file)', 'log.level=debug (arguments)'], result
    result = run(['show', 'x'])
    assert (result.returncode, result.stdout) == (64, 'show takes no requests\n' + HELP), result
    # A file that fails changes no setting and ends the program.
    with open(settings, 'w', encoding='utf-8') as target:
        target.write('{"capacity": 4, "colour": "red"}')
    result = run(['-c', settings, 'show'])
    assert (result.returncode, result.stdout) == (
        78, 'invalid configuration: unknown_key at 0\n'), result
    result = run(['-c', os.path.join(directory, 'missing.json'), 'show'])
    assert (result.returncode, result.stdout) == (70, 'service failed: not_found\n'), result


def read_all(connection):
    data = b''
    while True:
        chunk = connection.recv(64)
        if not chunk:
            return data
        data += chunk


def serve_until(stop_signal, requests, partial=None):
    """Serve the requests, then send stop_signal while the partial connection is being read."""
    process = subprocess.Popen([args.executable, 'serve'], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, env=ENVIRONMENT)
    try:
        line = process.stdout.readline()
        assert line.startswith('listening 127.0.0.1:'), line
        port = int(line.strip().rsplit(':', 1)[1])
        replies = []
        for request in requests:
            with socket.create_connection(('127.0.0.1', port), timeout=10) as connection:
                connection.sendall(request)
                connection.shutdown(socket.SHUT_WR)
                replies.append(read_all(connection))
        pending = None
        if partial is not None:
            pending = socket.create_connection(('127.0.0.1', port), timeout=10)
            pending.sendall(partial[0])
            # The handler reads when the signal arrives; the service timeout is one second.
            time.sleep(0.3)
        process.send_signal(stop_signal)
        if pending is not None:
            time.sleep(0.1)
            pending.sendall(partial[1])
            pending.shutdown(socket.SHUT_WR)
            replies.append(read_all(pending))
            pending.close()
        output, errors = process.communicate(timeout=30)
        return process.returncode, output, errors, replies
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


status, output, errors, replies = serve_until(signal.SIGTERM, [b'alpha', b'beta'],
                                              (b'gamma-', b'delta'))
assert status == 0, (status, errors)
assert replies == [b'alpha', b'beta', b'gamma-delta'], replies
assert output == ('stopped by SIGTERM\n'
                  'accepted=3 rejected=0 completed=3 failed=0 cancelled=0\n'), output
assert records(errors) == [starting('serve', 0), 'info message="stop requested" signal=SIGTERM',
                           stopped('info', 3, 3, 0)], errors
status, output, errors, replies = serve_until(signal.SIGINT, [])
assert (status, replies) == (0, []), (status, errors, replies)
assert output == 'stopped by SIGINT\naccepted=0 rejected=0 completed=0 failed=0 cancelled=0\n', output
assert records(errors) == [starting('serve', 0), 'info message="stop requested" signal=SIGINT',
                           stopped('info', 0, 0, 0)], errors
print(f'Service: {len(cases)} command and account checks, JSON and file records, layered '
      'settings and 2 signal stops passed')
