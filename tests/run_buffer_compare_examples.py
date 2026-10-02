#!/usr/bin/env python3
"""Check byte lengths and comparison while the program owns and erases both buffers."""
import argparse
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    for left,right in [('', ''),('abc','abc'),('abc','abd'),('abc','ab'),('caf\u00e9','caf\u00e9'),('a'*4096,'a'*4096)]:
        r=subprocess.run([exe,left,right],capture_output=True,text=True,timeout=10)
        expected=f'equal={str(left==right).lower()} expected_bytes={len(left.encode())} candidate_bytes={len(right.encode())}\nbuffers_erased\n'
        assert r.returncode==0 and not r.stderr and r.stdout==expected,(r,expected)
    print('Buffer comparison: six equality, length and ownership scenarios passed')


if __name__=='__main__':main()
