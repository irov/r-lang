#!/usr/bin/env python3
"""Test user-visible search, redaction, splitting and UTF-8 editing commands."""
import argparse
import re
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    count=0

    def run(args,expected,status=0,pattern=False):
        nonlocal count
        result=subprocess.run([exe,*args],capture_output=True,text=True,timeout=10)
        assert result.returncode==status and not result.stderr,(args,result)
        assert re.fullmatch(expected,result.stdout) if pattern else result.stdout==expected,(args,result.stdout,expected)
        count+=1

    run(['find',r'\d+','item=42 count=7'],'5:7\t42\n')
    run(['find-i','warn','WARN: retry'],'0:4\tWARN\n')
    run(['find','warn','WARN: retry'],'none\n')
    run(['all',r'\d+','item=42 count=7'],'5:7\t42\n14:15\t7\n')
    run(['from',r'\d+','item=42 count=7','7'],'14:15\t7\n')
    run(['check',r'\d+','42'],'match=true full=true\n')
    run(['check',r'\d+','x42'],'match=true full=false\n')
    run(['check',r'\d+','x'],'match=false full=false\n')
    run(['replace',r'\d+','item=42 count=7','#'],'item=# count=#\n')
    run(['split',r'[,;]\s*','one, two;three'],'one\ntwo\nthree\n')
    run(['split',',',',one,,'],'\none\n\n\n')
    run(['escape','a+b?.txt'],r'a\+b\?\.txt'+'\n')
    run(['all','.','\u00e9x'],'0:2\t\u00e9\n2:3\tx\n')
    run(['all','','ab'],'0:0\t\n1:1\t\n2:2\t\n')
    run(['all','^item','a\nitem\nitem'],'2:6\titem\n7:11\titem\n')
    run(['inspect','Abc\nABC','Ab'],'ascii=true newlines=1 prefix=true suffix=false contains=true folded_equal=false\nlower=abc\nabc\nfirst=0\n')
    run(['inspect','\u00e9','x'],'ascii=false newlines=0 prefix=false suffix=false contains=false folded_equal=false\nlower=\u00e9\nfirst=none\n')
    run(['message','INFO','ready','100'],r'INFO: ready\nbytes=11 scratch_capacity=\d+\n',pattern=True)
    run(['message','I','\u00e9x','5'],r'I: \u00e9\nbytes=5 scratch_capacity=\d+\n',pattern=True)
    run(['message','I','\u00e9x','4'],'limit splits a UTF-8 character\n',65)
    run(['find','[','text'],r'regex error [a-z_]+ at byte \d+\n',65,pattern=True)
    run(['from','.','\u00e9','1'],r'regex error [a-z_]+ at byte \d+\n',65,pattern=True)
    run(['find','a'],'wrong number of operands\n',64)
    print(f'Text: {count} search, redaction, inspection and UTF-8 editing checks passed')


if __name__=='__main__':main()
