#!/usr/bin/env python3
"""Compare bounded byte transport and UTF-8 validation with an independent decoder."""
import argparse
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',required=True)
    exe=parser.parse_args().executable
    count=0
    def run(mode,data,status=0,expected=None,diagnostic=b''):
        nonlocal count
        result=subprocess.run([exe,mode],input=data,capture_output=True,timeout=20)
        assert result.returncode==status,(mode,len(data),result)
        assert result.stdout==(data if expected is None else expected),(mode,len(data),len(result.stdout))
        assert result.stderr==diagnostic,(mode,result.stderr,diagnostic)
        count+=1
    binary=[b'',b'\0',bytes(range(256))*1800,b'x'*1048576]
    for mode in ['plain','shared']:
        for data in binary:run(mode,data)
    valid=[b'',b'ASCII\0text',('caf\u00e9 \U0001f642'*5000).encode(),b'x'*4095+'\u00e9'.encode()*4097]
    invalid=[b'\xff',b'abc\xc0\xaf',b'\xed\xa0\x80',b'x'*4095+b'\xe2\x82',b'valid\xf4\x90\x80\x80']
    for mode in ['text','text_async']:
        for data in valid:run(mode,data)
        for data in invalid:
            try:data.decode('utf-8')
            except UnicodeDecodeError as failure:offset=failure.start
            else:raise AssertionError('invalid test input was valid')
            run(mode,data,65,b'',f'invalid UTF-8 at byte {offset}\n'.encode())
    for mode in ['plain','shared','text','text_async']:
        run(mode,b'x'*1048577,65,b'',b'input exceeds 1 MiB\n')
    print(f'Byte pipe: {count} binary, shared-owner, UTF-8, chunk-boundary and size-limit checks passed')


if __name__=='__main__':main()
