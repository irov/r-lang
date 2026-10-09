"""Reading the run-time checks left in the optimized LLVM IR of a fixture.

A check the optimizer keeps ends in a cold call of r_runtime_raise or r_runtime_panic whose
arguments are the panic category and the source span of the checked place (RRuntimeSourceSpan,
passed as [2 x i64]: the module key and the start byte, then the end byte). Where the optimizer
merged the calls of several checks, the arguments are phis of those constants. The named
metadata !r.sources gives the module of each source key.
"""

from pathlib import Path
import re
import subprocess

# RRuntimePanicCategory (runtime/include/r_runtime_core.h).
BOUNDS = 2
INVALID_CONVERSION = 6

CALL = re.compile(r'call void @r_runtime_(?:raise|panic)\((i32 (?:-?\d+|%[\w.]+)), '
                  r'(\[2 x i64\] (?:\[i64 -?\d+, i64 -?\d+\]|%[\w.]+))\)')
PHI = re.compile(r'^\s*(%[\w.]+) = phi (?:i32|\[2 x i64\]) (.*)$', re.M)
INCOMING = re.compile(r'\[ (\[i64 -?\d+, i64 -?\d+\]|-?\d+|%[\w.]+), %[\w.]+ \]')
SPAN = re.compile(r'\[i64 (-?\d+), i64 (-?\d+)\]')
DEFINE = re.compile(r'^define [^@]*@("[^"]+"|[\w.$]+)\(', re.M)
SOURCE = re.compile(r'^!\d+ = !\{i32 (\d+), !"([^"]*)"\}$', re.M)
SOURCES = re.compile(r'^!r\.sources = !\{([^}]*)\}$', re.M)
TRIPLE = re.compile(r'^(!\d+) = !\{i32 (\d+), i32 (\d+), i32 (\d+)\}$', re.M)
MODULE = re.compile(rb'^module\s+([A-Za-z_][\w.]*)\s*;', re.M)


def emit(front, fixture, extra=()):
    """The optimized IR of every function of the fixture, each kept as if called elsewhere."""
    result = subprocess.run([front, '--emit=llvm-ir', '--all-functions', *extra, str(fixture)],
                            capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        raise SystemExit(f'{Path(fixture).name}: r-front failed\n{result.stderr}')
    return result.stdout


def functions(ir):
    """(name, body) of each defined function."""
    starts = [(match.start(), match.group(1).strip('"')) for match in DEFINE.finditer(ir)]
    for index, (start, name) in enumerate(starts):
        end = ir.index('\n}\n', start) + 3
        yield name, ir[start:end]


def _values(phis, value, seen=()):
    if not value.startswith('%'):
        return [value]
    if value not in phis or value in seen:
        raise SystemExit(f'cannot read the argument {value} of a panic call')
    result = []
    for incoming in phis[value]:
        result += _values(phis, incoming, seen + (value,))
    return result


def checks(ir):
    """(function, category, module key, start byte) of every check left in the IR."""
    found = []
    for name, body in functions(ir):
        phis = {match.group(1): INCOMING.findall(match.group(2)) for match in PHI.finditer(body)}
        for call in CALL.finditer(body):
            category = call.group(1).split(' ', 1)[1]
            span = call.group(2)[len('[2 x i64] '):]
            categories = _values(phis, category)
            spans = _values(phis, span)
            if len(set(categories)) == 1:
                pairs = [(categories[0], value) for value in spans]
            elif len(categories) == len(spans):
                pairs = list(zip(categories, spans))
            else:
                raise SystemExit(f'{name}: cannot pair the categories and spans of a panic call')
            for value, span_value in pairs:
                low = int(SPAN.match(span_value).group(1)) & 0xFFFFFFFFFFFFFFFF
                found.append((name, int(value), low & 0xFFFFFFFF, low >> 32))
    return found


def source_key(ir, fixture):
    """The source key of the fixture's module in !r.sources."""
    module = MODULE.search(Path(fixture).read_bytes())
    if module is None:
        raise SystemExit(f'{Path(fixture).name}: no module declaration')
    listed = SOURCES.search(ir)
    if listed is None:
        raise SystemExit('the IR has no !r.sources')
    entries = {}
    for match in SOURCE.finditer(ir):
        entries[match.group(0).split(' ', 1)[0]] = (int(match.group(1)), match.group(2))
    for node in (item.strip() for item in listed.group(1).split(',')):
        key, name = entries[node]
        if name == module.group(1).decode():
            return key
    raise SystemExit(f'{Path(fixture).name}: its module is not in !r.sources')


def checked_lines(ir, fixture):
    """{(conversion, line)} of the checks left in the fixture's own source."""
    key = source_key(ir, fixture)
    source = Path(fixture).read_bytes()
    lines = set()
    for _, category, module, start in checks(ir):
        if module == key and category in (BOUNDS, INVALID_CONVERSION):
            lines.add((category == INVALID_CONVERSION, source[:start].count(b'\n') + 1))
    return lines


def listed_lines(ir, fixture, name):
    """The lines of the fixture whose places the named metadata `name` lists by source key and
    bytes (!r.direct, !r.versioned)."""
    listed = re.search(r'^!' + re.escape(name) + r' = !\{([^}]*)\}$', ir, re.M)
    if listed is None:
        return set()
    key = source_key(ir, fixture)
    nodes = {match.group(1): (int(match.group(2)), int(match.group(3)))
             for match in TRIPLE.finditer(ir)}
    source = Path(fixture).read_bytes()
    lines = set()
    for node in (item.strip() for item in listed.group(1).split(',')):
        module, start = nodes[node]
        if module == key:
            lines.add(source[:start].count(b'\n') + 1)
    return lines


def direct_lines(ir, fixture):
    """The lines of the fixture whose await may complete without its task."""
    return listed_lines(ir, fixture, 'r.direct')
