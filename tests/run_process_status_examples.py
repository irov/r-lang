#!/usr/bin/env python3
"""Check orderly exit mapping and deliberate abnormal child termination."""
import argparse
import resource
import signal
import subprocess


def no_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',required=True)
    exe=parser.parse_args().executable
    for value,expected in [('0',0),('23',23),('255',255),('invalid',64)]:
        result=subprocess.run([exe,value],capture_output=True,timeout=10)
        assert result.returncode==expected and not result.stdout and not result.stderr,result
    result=subprocess.run([exe,'abort'],capture_output=True,timeout=10,preexec_fn=no_core_dump)
    assert result.returncode==-signal.SIGABRT and not result.stdout and not result.stderr,result
    print('Process probe: orderly status, invalid input and SIGABRT passed')


if __name__=='__main__': main()
