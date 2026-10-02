#!/usr/bin/env python3
"""Exercise real children, bounded concurrent pipes and terminal process outcomes."""
import argparse
import os
import re
import subprocess
import sys
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',required=True)
    exe=parser.parse_args().executable
    def run(mode,cwd,program,*arguments,status=0):
        result=subprocess.run([exe,mode,cwd,program,*arguments],capture_output=True,text=True,timeout=20)
        assert result.returncode==status and not result.stderr,result
        if status==0:
            match=re.search(r'^pid=(\d+)\n',result.stdout,re.M)
            assert match and int(match[1])>0,result
        return re.sub(r'^pid=\d+\n','',result.stdout,flags=re.M)
    assert run('run','/','/bin/echo','hello world')=='hello world\nkind=exited code=0 success=true\n'
    assert run('capture','/','/bin/echo','hello world')=='stdout:\nhello world\n\nstderr:\n\nkind=exited code=0 success=true\n'
    code='import sys; print("normal"); print("diagnostic",file=sys.stderr); sys.exit(7)'
    assert run('capture','/',sys.executable,'-c',code)=='stdout:\nnormal\n\nstderr:\ndiagnostic\n\nkind=exited code=7 success=false\n'
    # Each stream exceeds pipe capacity. Sequential reading would deadlock this child.
    code='import os; os.write(2,b"E"*150000); os.write(1,b"O"*150000)'
    expected='stdout:\n'+'O'*150000+'\nstderr:\n'+'E'*150000+'\nkind=exited code=0 success=true\n'
    assert run('capture','/',sys.executable,'-c',code)==expected
    # UTF-8 scalars straddle arbitrary read boundaries.
    code='import sys; sys.stdout.write("x"*4095+"\u00e9"*4097)'
    expected='stdout:\n'+'x'*4095+'\u00e9'*4097+'\nstderr:\n\nkind=exited code=0 success=true\n'
    assert run('capture','/',sys.executable,'-c',code)==expected
    code='import sys; print(len(sys.stdin.buffer.read()))'
    assert run('capture','/',sys.executable,'-c',code)=='stdout:\n0\n\nstderr:\n\nkind=exited code=0 success=true\n'
    with tempfile.TemporaryDirectory(prefix='r-runner-') as cwd:
        code='import os; print(os.getcwd()); print(os.getenv("R_EXAMPLE_VALUE")); print(os.getenv("R_EXAMPLE_REMOVED"))'
        result=run('capture',cwd,sys.executable,'--clear-env','--env','R_EXAMPLE_VALUE','a b','--env','R_EXAMPLE_REMOVED','x','--unset','R_EXAMPLE_REMOVED','--','-c',code)
        assert result=='stdout:\n'+os.path.realpath(cwd)+'\na b\nNone\n\nstderr:\n\nkind=exited code=0 success=true\n',result
    assert re.fullmatch(r'kind=signalled code=\d+ success=false\n',run('stop','/','/bin/sleep','60'))
    run('capture','/','/r-example-no-such-executable',status=69)
    run('capture','/r-example-no-such-directory','/bin/echo',status=69)
    run('capture','/',sys.executable,'-c','import os; os.write(1,b"\\xff")',status=64)
    run('capture','/',sys.executable,'-c','import os; os.write(1,b"X"*1048577)',status=64)
    print('Runner: execution, arguments, environment, cwd, dual pipes, UTF-8, EOF, exit, kill and failures passed')


if __name__=='__main__': main()
