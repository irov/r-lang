#!/usr/bin/env python3
"""Check argument snapshots and mutations isolated to the example child process."""
import argparse
import os
import re
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',required=True)
    exe=parser.parse_args().executable
    environment=dict(os.environ,R_EXAMPLE_ENV_PRESENT='original')
    environment.pop('R_EXAMPLE_ENV_ABSENT',None)
    def run(args,expected=None,status=0):
        result=subprocess.run([exe,*args],env=environment,capture_output=True,text=True,timeout=10)
        assert result.returncode==status and not result.stderr,result
        if expected is not None: assert result.stdout==expected,result
        return result.stdout
    run(['get','R_EXAMPLE_ENV_PRESENT'],'R_EXAMPLE_ENV_PRESENT=original\n')
    run(['get','R_EXAMPLE_ENV_ABSENT'],'R_EXAMPLE_ENV_ABSENT=<absent>\n')
    for value in ['changed','', 'two words', 'caf\u00e9']:
        run(['set','R_EXAMPLE_ENV_PRESENT',value],f'R_EXAMPLE_ENV_PRESENT={value}\n')
        run(['get','R_EXAMPLE_ENV_PRESENT'],'R_EXAMPLE_ENV_PRESENT=original\n')
    run(['unset','R_EXAMPLE_ENV_PRESENT'],'R_EXAMPLE_ENV_PRESENT=<absent>\n')
    run(['unset','R_EXAMPLE_ENV_ABSENT'],'R_EXAMPLE_ENV_ABSENT=<absent>\n')
    for name in ['', 'A=B']:
        run(['set',name,'x'],'invalid environment variable name\n',65)
        run(['get',name],'invalid environment variable name\n',65)
    args=['arguments','one','two words','caf\u00e9']
    expected=f'arguments={len(args)+1}\n'+''.join(f'{i}: {v}\n' for i,v in enumerate([exe,*args]))
    run(args,expected)
    count=run(['summary'])
    assert re.fullmatch(r'environment_entries=\d+\n',count),count
    assert int(count.split('=')[1])==len(environment),count
    print('Environment: snapshots, Unicode, empty/absent values, mutation isolation and invalid names passed')


if __name__=='__main__': main()
