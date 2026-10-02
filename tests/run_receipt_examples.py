#!/usr/bin/env python3
"""Check captured templates, repeated positional slots and reusable builder output."""
import argparse
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    for name,quantity,price in [('Ada',3,1.25),('caf\u00e9',0,2.5),('A {1}',2,4.5)]:
        total=quantity*price
        expected=f'Receipt for {name}\n{name}: {quantity} x {price:.2f} = {total:.2f} ({quantity} units)\n'
        expected+=f'Empty-order preview: {name}: 0 x {price:.2f} = 0.00 (0 units)\nRaw total: {total:g}\n'
        r=subprocess.run([exe,name,str(quantity),str(price)],capture_output=True,text=True,timeout=10)
        assert r.returncode==0 and not r.stderr and r.stdout==expected,(r,expected)
    r=subprocess.run([exe,'Ada','-1','2'],capture_output=True,text=True,timeout=10)
    assert r.returncode==64 and 'nonnegative' in r.stdout and not r.stderr,r
    print('Receipt: named snapshots, repeated slots, empty orders and invalid quantities passed')


if __name__=='__main__':main()
