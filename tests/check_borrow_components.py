#!/usr/bin/env python3
"""Check source imports and stable, component-specific borrow interface records."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-borrow-components-') as directory:
        root = Path(directory)
        api = root / 'api.r'
        app = root / 'app.r'
        api.write_text('''module components.api;
struct Pair { const i32* first; const i32* second; };
Pair views(const i32* first, const i32* second) {
    return Pair {.first=first, .second=second};
}
const i32* choose(Pair value) { return value.second; }
error Failure { Pair view; };
void reject(const i32* first, const i32* second) throws Failure {
    throw Failure {.view=Pair {.first=first, .second=second}};
}
@generic<T: copy> struct Envelope { T first; T second; };
@generic<T: copy> Envelope<T> wrap(T first, T second) {
    return Envelope<T> {.first=first, .second=second};
}
''')
        app.write_text('''module components.app;
import components.api;
i32 main() {
    own i32* first=new i32(20);
    own i32* second=new i32(22);
    const i32* a=&*first;
    const i32* b=&*second;
    components.api::Envelope<const i32*> generic=components.api::wrap(a,b);
    components.api::Pair pair=components.api::views(a,b);
    const i32* selected=components.api::choose(pair);
    const i32* other=generic.second;
    drop first;
    return *selected + *other - 44;
}
''')

        def emit(mode, paths):
            result = subprocess.run([args.front, '--emit=' + mode, *map(str, paths)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (result.returncode, result.stderr)
            return result.stdout

        interface = emit('interface', [api, app])
        assert '(interface version=34 ' in interface
        assert interface == emit('interface', [app, api])
        assert emit('c17', [api, app]) == emit('c17', [app, api])
        views = next(line for line in interface.splitlines()
                     if '(function name="components.api::views"' in line)
        assert 'output=((field "first")) input=0 path=()' in views
        assert 'output=((field "second")) input=1 path=()' in views
        choose = next(line for line in interface.splitlines()
                      if '(function name="components.api::choose"' in line)
        assert 'output=() input=0 path=((field "second"))' in choose
        reject = next(line for line in interface.splitlines()
                      if '(function name="components.api::reject"' in line)
        assert 'components=(complete=true' in reject
        assert 'output=((field "view") (field "second")) input=1 path=()' in reject
        closed = [line for line in interface.splitlines()
                  if '(function name="components.api::wrap<' in line]
        assert len(closed) == 1 and 'output=((field "second")) input=1 path=()' in closed[0]

        api.write_text(api.read_text().replace('.second=second', '.second=first'))
        invalid = subprocess.run([args.front, '--emit=c17', str(app), str(api)],
                                 capture_output=True, text=True, timeout=30)
        assert invalid.returncode != 0 and 'R-DIAG-BORROW-001' in invalid.stderr
    print('borrow components: imported paths, errors, generic instances and stable metadata passed')


if __name__ == '__main__':
    main()
