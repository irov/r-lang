#!/usr/bin/env python3
"""Audit example source references and their registered runtime checks.

References are an index, not evidence that every branch executed. Closed API families
are covered only when every concrete operation is referenced or resolved in HIR. Walkthroughs are distinguished
from applications with observable command-line behavior.
"""
from __future__ import annotations

import argparse
import glob
import json
from pathlib import Path
import re
import sys
import subprocess

from generate_numeric_example import R_INTEGERS, C_INTEGERS, FLOATS

ROOT = Path(__file__).resolve().parents[1]


def code_only(text: str) -> str:
    # Format-string expressions are deliberately not counted as evidence.
    return re.sub(
        r'/\*[\s\S]*?\*/|//[^\n]*|(?:f)?"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        ' ', text,
    )


WIDE_INTEGER_FAMILIES = {
    f'core::{name}_SUFFIX' for name in ('widening_mul', 'carrying_add', 'borrowing_sub', 'narrowing_div')
}


def spellings(item: dict, items: list[dict]) -> list[str]:
    name = item['id']
    if name.endswith(('_S', '_C')):
        prefix = name[:-1]
        required = sorted({
            other['id'] for other in items
            if other['id'].startswith(prefix) and other['id'] != name
            and other['item_kind'] not in {'operation_schema', 'public_type_schema'}
        })
        if not required:
            raise ValueError(f'closed API family has no concrete records: {name}')
        return required
    if name.endswith('_SUFFIX'):
        suffixes = R_INTEGERS if name.startswith('core::') else R_INTEGERS + C_INTEGERS
        if name in WIDE_INTEGER_FAMILIES:
            # Library R-LIB-0027: the wide operations exist for the unsigned types only.
            suffixes = tuple(suffix for suffix in R_INTEGERS if suffix.startswith('u'))
        return [name[:-6] + suffix for suffix in suffixes]
    if name.endswith('_D'):
        suffixes = (R_INTEGERS + C_INTEGERS + FLOATS if name.startswith('std.convert::')
                    else C_INTEGERS + ('c_float', 'c_double', 'c_long_double'))
        return [name[:-1] + suffix for suffix in suffixes]
    return [name]


def source_files(app: dict) -> list[Path]:
    files = set()
    for pattern in app['sources']:
        matches = glob.glob(str(ROOT / pattern))
        if not matches:
            raise ValueError(f"empty source pattern in {app['id']}: {pattern}")
        for match in matches:
            file = Path(match).resolve()
            relative = file.relative_to(ROOT)
            if relative.parts[0] != 'examples' or file.suffix != '.r':
                raise ValueError(f'non-example source in catalogue: {relative}')
            files.add(file)
    return sorted(files)


def validate_catalogue(catalogue: dict, registered_tests: set[str] | None) -> None:
    identifiers = set()
    for app in catalogue['applications']:
        identifier = app['id']
        if identifier in identifiers:
            raise ValueError(f'duplicate example: {identifier}')
        identifiers.add(identifier)
        if app['kind'] not in {'application', 'walkthrough'}:
            raise ValueError(f'unknown example kind: {identifier}')
        if not (ROOT / 'examples' / identifier / 'README.md').is_file():
            raise ValueError(f'missing README: {identifier}')
        files = source_files(app)
        tests = app.get('tests', [])
        if not tests or len(set(tests)) != len(tests):
            raise ValueError(f'missing or repeated runtime checks: {identifier}')
        if registered_tests is not None:
            missing = set(tests) - registered_tests
            if missing:
                raise ValueError(f"unregistered tests for {identifier}: {sorted(missing)}")
        if 'behaviour' in app and not (ROOT / app['behaviour']).is_file():
            raise ValueError(f'missing command checks: {identifier}')
        if 'map' in app:
            path = ROOT / app['map']
            mappings = {}
            for line in path.read_text().splitlines():
                line = line.split('#', 1)[0].strip()
                if not line:
                    continue
                module, target = map(str.strip, line.split('=', 1))
                if module in mappings:
                    raise ValueError(f'duplicate module in {path}: {module}')
                file = (path.parent / target).resolve()
                if not file.is_file():
                    raise ValueError(f'missing module source: {file}')
                mappings[module] = file
            if app['entry'] not in mappings or mappings[app['entry']] not in files:
                raise ValueError(f'entry is not a catalogued source: {identifier}')


def hir_types(hir: str, modules: set[str], *, operations: bool = False) -> set[str]:
    """Read program references, excluding implicit main and propagation allowances."""
    stack = []
    found = set()
    for token in re.findall(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()"]+', hir):
        if token == '(':
            parent = stack[-1] if stack else {}
            stack.append({'head': None, 'count': 0, 'program': parent.get('program', False),
                          'module': parent.get('module'),
                          'implicit_main_effects': parent.get('implicit_main_effects', False) or
                          parent.get('last') == 'propagate=' or
                          (parent.get('head') == 'function' and parent.get('main', False)
                           and parent.get('last') in {'throws=', 'start='})})
            parent['last'] = None
        elif token == ')':
            if not stack:
                raise ValueError('unbalanced HIR close')
            stack.pop()
        elif stack:
            frame = stack[-1]
            if frame['head'] is None:
                frame['head'] = token
                if token == 'program':
                    frame['program'] = True
            else:
                frame['count'] += 1
                if frame['head'] == 'function' and token == 'name=':
                    frame['await_name'] = True
                elif frame.pop('await_name', False):
                    frame['main'] = token == '"main"'
                frame['last'] = token
                if frame.get('implicit_main_effects', False):
                    continue
                if frame['head'] == 'module' and frame['count'] == 1:
                    frame['module'] = token[1:-1] if token.startswith('"') else None
                active = frame['program'] and frame['module'] in modules
                if operations:
                    if active and token.startswith('operation='):
                        found.add(token.removeprefix('operation='))
                    if active and frame.get('source_operation') and token.startswith('"'):
                        found.add(json.loads(token))
                    frame['source_operation'] = token == 'source_operation='
                    continue
                if frame['head'] in {'struct', 'enum'}:
                    if frame['count'] == 1:
                        frame['owner'] = token[1:-1] if token.startswith('"') else ''
                    if (frame['count'] == 3 and token.startswith('"') and frame['program']
                            and frame['module'] in modules
                            and re.fullmatch(r'std\.[a-z][a-z0-9_]*', frame.get('owner', ''))):
                        name = re.split(r'[<(]', token[1:-1], maxsplit=1)[0]
                        if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
                            found.add(frame['owner'] + '::' + name)
                if (frame['head'] == 'standard' and frame['count'] == 1 and frame['program']
                        and frame['module'] in modules and
                        re.fullmatch(r'"(?:core|std\.[a-z][a-z0-9_]*)::[A-Za-z_][A-Za-z0-9_]*"', token)):
                    found.add(token[1:-1])
    if stack:
        raise ValueError('unbalanced HIR open')
    return found


def resolved_references(app: dict, frontend: Path) -> tuple[set[str], set[str]]:
    files = source_files(app)
    modules = set()
    for file in files:
        modules.update(re.findall(r'\bmodule\s+([A-Za-z_][A-Za-z0-9_.]*)\s*;', code_only(file.read_text())))
    command = [str(frontend), '--emit=hir', '--library-map', str(ROOT / 'library/r/library.map')]
    if app.get('test_mode'):
        # R-FUNC-0025 (M24): a test program resolves with its generated test entry.
        command.append('--test')
    if app.get('link_manifest'):
        manifest = ROOT / app['link_manifest']
        if not manifest.is_file():
            raise ValueError(f'missing example link manifest: {manifest}')
        command += ['--link-manifest', str(manifest)]
    module_map = ROOT / app.get('map', f"examples/{app['id']}/modules.map")
    if module_map.exists():
        command += ['--module-map', str(module_map)]
    if app.get('entry'):
        commands = [command + ['--entry', app['entry']]]
    elif module_map.exists():
        candidates = [file for file in files if re.search(r'\bmain\s*\(', code_only(file.read_text()))]
        if not candidates:
            raise ValueError(f"no application entry in {app['id']}")
        commands = []
        for file in candidates:
            module = re.search(r'\bmodule\s+([A-Za-z_][A-Za-z0-9_.]*)\s*;', code_only(file.read_text()))
            if module is None:
                raise ValueError(f'missing module in {file}')
            commands.append(command + ['--entry', module.group(1)])
    else:
        commands = [command + [str(file)] for file in files]
    found = set()
    operations = set()
    for invocation in commands:
        # A sanitized r-front needs 20-30 s for the largest applications and several times that
        # while a test battery loads the machine; the limit only has to catch a hang.
        result = subprocess.run(invocation, capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise ValueError(f"cannot resolve example {app['id']}: {result.stderr}")
        found.update(hir_types(result.stdout, modules))
        operations.update(hir_types(result.stdout, modules, operations=True))
    return found, operations


def report(registered_tests: set[str] | None = None, frontend: Path | None = None) -> dict:
    catalogue = json.loads((ROOT / 'examples/catalogue.json').read_text())
    inventory = json.loads(
        (ROOT / 'library/generated/api_inventory/implementation_inventory.json').read_text())
    validate_catalogue(catalogue, registered_tests)
    references: dict[str, set[str]] = {}
    applications = {app['id'] for app in catalogue['applications'] if app['kind'] == 'application'}
    for app in catalogue['applications']:
        for file in source_files(app):
            for name in re.findall(
                    r'\b(?:core|std\.[a-z][a-z0-9_]*)::[A-Za-z_][A-Za-z0-9_]*',
                    code_only(file.read_text())):
                references.setdefault(name, set()).add(app['id'])
    typed_references: dict[str, set[str]] = {}
    if frontend is not None:
        for app in catalogue['applications']:
            types, operations = resolved_references(app, frontend)
            for name in types:
                typed_references.setdefault(name, set()).add(app['id'])
            for name in operations:
                references.setdefault(name, set()).add(app['id'])
    rows = []
    for item in inventory['items']:
        required = spellings(item, inventory['items'])
        evidence = references
        if item['item_kind'] in {'public_type', 'public_type_schema'}:
            evidence = {name: references.get(name, set()) | typed_references.get(name, set())
                        for name in required}
        missing = [name for name in required if not evidence.get(name)]
        application_missing = [name for name in required
                               if not (evidence.get(name, set()) & applications)]
        rows.append({
            'record_id': item['record_id'], 'required': required, 'missing': missing,
            'application_missing': application_missing,
            'examples': sorted({app for name in required for app in evidence.get(name, set())}),
            'resolved_type_examples': sorted(typed_references.get(item['id'], set()))
                if item['item_kind'] in {'public_type', 'public_type_schema'} else [],
        })
    return {
        'version': 4, 'scope': 'source references and resolved program operations and types; execution checked separately',
        'total_records': len(rows), 'covered_records': sum(not row['missing'] for row in rows),
        'application_records': sum(not row['application_missing'] for row in rows), 'items': rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true')
    mode.add_argument('--check', action='store_true')
    parser.add_argument('--require-complete', action='store_true')
    parser.add_argument('--frontend', type=Path, default=ROOT / 'build-debug/r-front')
    parser.add_argument('--ctest-json', type=Path, help='CTest --show-only=json-v1 output')
    args = parser.parse_args()
    registered = None
    if args.ctest_json:
        registered = {test['name'] for test in json.loads(args.ctest_json.read_text())['tests']}
    try:
        data = report(registered, args.frontend.resolve())
    except (ValueError, KeyError, OSError, subprocess.TimeoutExpired) as failure:
        parser.exit(1, f'Example catalogue: {failure}\n')
    text = json.dumps(data, indent=2) + '\n'
    destination = ROOT / 'examples/coverage.json'
    if args.write:
        destination.write_text(text)
    if args.check and (not destination.is_file() or destination.read_text() != text):
        parser.exit(1, 'Example coverage is stale; run tools/check_example_coverage.py --write\n')
    print(f"Examples: {data['covered_records']}/{data['total_records']} public records referenced; "
          f"{data['application_records']} in applications")
    if args.require_complete and data['application_records'] != data['total_records']:
        for row in data['items']:
            if row['application_missing']:
                print(row['record_id'] + ': ' + ', '.join(row['application_missing']), file=sys.stderr)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
