#!/usr/bin/env python3
"""Index syntax nodes in example CSTs, separately from runtime and API coverage."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

from check_example_coverage import ROOT, source_files, validate_catalogue

# These are parser bookkeeping and recovery nodes, not positive language examples.
EXCLUDED = {'invalid', 'error', 'token', 'ambiguous_decl_or_expr', 'external_declaration',
            'translation_unit', 'expression', 'primary_expression', 'postfix_expression',
            'initializer_item', 'parameter_list', 'argument_list', 'type',
            # Call-free constructor classification has the same source syntax as a call suffix.
            'constructor_suffix'}


def syntax_names() -> set[str]:
    source = (ROOT / 'compiler/source/frontend.c').read_text()
    start = source.index('const char *r_syntax_kind_name(')
    end = source.index('};', start)
    names = set(re.findall(r'"([a-z_]+)"', source[start:end])) - EXCLUDED
    parser_sources = '\n'.join(path.read_text() for path in (ROOT / 'compiler/parser').rglob('*')
                               if path.suffix in {'.c', '.h', '.inc'})
    active = {name.lower() for name in re.findall(r'R_SYNTAX_([A-Z_]+)', parser_sources)}
    return names & active


def node_names(cst: str) -> set[str]:
    return set(re.findall(r'^\s*\(([a-z_]+)(?:\s|$)', cst, re.MULTILINE)) - EXCLUDED


def report(frontend: Path) -> dict:
    catalogue = json.loads((ROOT / 'examples/catalogue.json').read_text())
    validate_catalogue(catalogue, None)
    cache = {}
    references = {name: set() for name in syntax_names()}
    applications = {app['id'] for app in catalogue['applications'] if app['kind'] == 'application'}
    for app in catalogue['applications']:
        for file in source_files(app):
            if file not in cache:
                command = [str(frontend), '--emit=cst', '--library-map', str(ROOT / 'library/r/library.map')]
                module_map = ROOT / app.get('map', f"examples/{app['id']}/modules.map")
                if module_map.exists():
                    command += ['--module-map', str(module_map)]
                command.append(str(file))
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                if result.returncode:
                    raise ValueError(f'cannot parse {file.relative_to(ROOT)}: {result.stderr}')
                # A module map loads dependencies for parsing. Count only the requested unit,
                # never nodes contributed by a standard library or another imported file.
                first_unit = result.stdout.split('\n; source ', 1)[0]
                if first_unit.startswith('; source '):
                    reported = first_unit.splitlines()[0][len('; source '):]
                    if Path(reported).resolve() != file:
                        raise ValueError(f'CST source order differs for {file}')
                cache[file] = node_names(first_unit)
            for node in cache[file]:
                if node not in references:
                    continue  # A view may introduce a non-parser normalization node.
                references[node].add(app['id'])
    rows = [{'node': name, 'applications': sorted(found & applications),
             'walkthroughs': sorted(found - applications)} for name, found in sorted(references.items())]
    return {'version': 1, 'scope': 'CST node presence; not semantic or branch coverage',
            'total_nodes': len(rows), 'application_nodes': sum(bool(row['applications']) for row in rows),
            'nodes': rows}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', required=True, type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true')
    mode.add_argument('--check', action='store_true')
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    try:
        result = report(args.frontend.resolve())
        content = json.dumps(result, indent=2) + '\n'
        destination = ROOT / 'examples/syntax-coverage.json'
        if args.write:
            destination.write_text(content)
        if args.check and (not destination.exists() or destination.read_text() != content):
            raise ValueError('stale syntax coverage; run --write with the current frontend')
        if args.require_complete and result['application_nodes'] != result['total_nodes']:
            missing = [row['node'] for row in result['nodes'] if not row['applications']]
            raise ValueError(f'missing application syntax: {", ".join(missing)}')
        print(f"Example syntax: {result['application_nodes']}/{result['total_nodes']} nodes in applications")
    except (ValueError, OSError, subprocess.SubprocessError) as failure:
        parser.exit(1, f'{failure}\n')


if __name__ == '__main__':
    main()
