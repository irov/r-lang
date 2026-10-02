#!/usr/bin/env python3
"""Run playlist editing scripts and compare every transition with a Python sequence."""
import argparse
import random
import subprocess

ARITY={'append':1,'prepend':1,'before':2,'after':2,'set':2,'set_first':1,'set_last':1,
       'first':0,'last':0,'get':1,'remove':1,'pop_first':0,'pop_last':0,'clear':0,'show':0}


def expected(script):
    tracks=[]
    output=[]
    for command,*operands in script:
        index=operands[0] if operands else 0
        value=operands[-1] if operands else 0
        if command=='append': tracks.append(value); output.append(f'appended={value}')
        elif command=='prepend': tracks.insert(0,value); output.append(f'prepended={value}')
        elif command=='show': output.append('tracks'+''.join(f' {v}' for v in tracks))
        elif command=='clear': tracks.clear(); output.append('cleared')
        else:
            if command in {'first','set_first','pop_first'}: index=0
            elif command in {'last','set_last','pop_last'}: index=len(tracks)-1
            if not 0<=index<len(tracks): output.append('missing'); continue
            if command in {'first','last','get'}: output.append(f'track={tracks[index]}')
            elif command in {'set','set_first','set_last'}: tracks[index]=value; output.append(f'set={value}')
            elif command in {'pop_first','pop_last','remove'}: output.append(f'removed={tracks.pop(index)}')
            else: tracks.insert(index+(command=='after'),value); output.append(f'inserted={value}')
    return '\n'.join(output)+'\n'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    scripts=[[
        ('append',20),('prepend',10),('before',1,15),('after',2,25),('show',),
        ('get',1),('first',),('last',),('set',1,16),('set_first',11),('set_last',26),
        ('remove',2),('pop_first',),('pop_last',),('show',),('clear',),('show',),
        ('get',99),('remove',0),('first',),('last',),('set_first',0),('set_last',0),
        ('pop_first',),('pop_last',),('before',0,1),('after',0,1)]]
    rng=random.Random(2031)
    for _ in range(8):
        script=[]
        for _ in range(40):
            command=rng.choice(list(ARITY))
            args=[rng.randrange(6) for _ in range(ARITY[command])]
            script.append((command,*args))
        script.append(('show',))
        scripts.append(script)
    for script in scripts:
        args=[str(value) for command in script for value in command]
        r=subprocess.run([exe,*args],capture_output=True,text=True,timeout=10)
        assert r.returncode==0 and not r.stderr and r.stdout==expected(script),(script,r,expected(script))
    for args,message in [(['set','1'],'missing command operand\n'),(['unknown'],'unknown playlist command\n')]:
        r=subprocess.run([exe,*args],capture_output=True,text=True,timeout=10)
        assert r.returncode==64 and r.stdout==message and not r.stderr,r
    print('Playlist: all commands, missing positions and eight deterministic editing scripts passed')


if __name__=='__main__':main()
