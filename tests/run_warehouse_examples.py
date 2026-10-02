#!/usr/bin/env python3
"""Verify warehouse command results and inventory updates."""
import argparse
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    cases=[
        ('set 101 8 set 102 4 get 101 add 101 -3 has 102 set 102 9 get 102 remove 102 has 102 show',
         'previous=none\nprevious=none\nquantity=8\nquantity=5\npresent=true\nprevious=4\nquantity=9\nremoved=9\npresent=false\nsku quantity\n101 5\n',0),
        ('reserve 40 set 1 7 clear show reset get 1 remove 1 add 1 2',
         'reserved\nprevious=none\ncleared\nsku quantity\nreset\nmissing\nremoved=none\nmissing\n',0),
        ('set 1 3 add 1 -4','insufficient inventory\n',64),
        ('set 1 -1','inventory cannot be negative\n',64),
        ('set 1 2147483647 add 1 1','inventory overflow\n',64),
        ('set 1','missing command operand\n',64),
    ]
    for script,expected,status in cases:
        r=subprocess.run([exe,*script.split()],capture_output=True,text=True,timeout=10)
        assert r.returncode==status and not r.stderr and r.stdout==expected,(script,r,expected)
    print('Warehouse: all commands and invalid inventory transitions passed')


if __name__=='__main__':main()
