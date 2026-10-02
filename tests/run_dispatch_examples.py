#!/usr/bin/env python3
"""Check route planning, scheduling, reconciliation, and the async message retry policy."""
import argparse
from collections import Counter
import random
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',required=True)
    exe=parser.parse_args().executable
    count=0
    def run(args,expected,status=0):
        nonlocal count
        result=subprocess.run([exe,*map(str,args)],capture_output=True,text=True,timeout=10)
        assert result.returncode==status and not result.stderr,(args,result)
        assert result.stdout==expected,(args,result.stdout,expected)
        count+=1
    def boolean(value): return str(value).lower()
    rng=random.Random(413)
    sequences=[[],[7],[3,1,3,2],list(range(30)),[rng.randrange(-20,120) for _ in range(70)]]
    for values in sequences:
        for mode in ['route','returning','cancel']:
            for omit in sorted({0,len(values)//2,len(values)}):
                stops=list(reversed(values)) if mode=='returning' else list(values)
                if omit: stops=stops[:-omit]
                if mode=='cancel': stops=[]
                expected=f'stops={len(stops)} empty={boolean(not stops)}\n'
                if stops: expected+=f'first={stops[0]}\nlast={stops[-1]}\n'
                expected+='route:'+''.join(f' {v}' for v in stops)+'\n'
                run([mode,omit,*values],expected)
        priorities=[max(0,min(v,100)) for v in values]
        jobs=sorted(enumerate(priorities),key=lambda item:(-item[1],item[0]))
        for limit in sorted({0,1,len(values)+2}):
            expected=f'jobs={len(values)}\n'
            if jobs:
                low,high=min(priorities),max(priorities)
                expected+=f'min={low} max={high} varied={boolean(low<high)} uniform={boolean(low==high)}\n'
                expected+=f'next={jobs[0][0]}:{jobs[0][1]}\n'
            expected+='assigned:'+''.join(f' {i}:{p}' for i,p in jobs[:limit])+'\n'
            remaining=max(len(values)-limit,0)
            expected+=f'remaining={remaining} empty={boolean(remaining==0)}\n'
            run(['priority',limit,*values],expected)
        for withdrawn in [1,999]:
            for mode in ['roster','pause']:
                bookings=Counter(values)
                keys=sorted(bookings)
                position=sum(k<withdrawn for k in keys)
                present=boolean(withdrawn in bookings)
                expected=f'members={len(keys)} registered={present} sorted={present} booked={present} position={position} booking_position={position}\n'
                if withdrawn in bookings:
                    expected+=f'withdrawn={withdrawn} requests={bookings.pop(withdrawn)}\n'
                arrival=list(dict.fromkeys(v for v in values if v!=withdrawn))
                if mode=='pause': arrival=[]
                expected+=f'dispatchable={len(arrival)} empty={boolean(not arrival)}\narrival:'
                expected+=''.join(f' {v}' for v in arrival)+'\nbookings:'
                expected+=''.join(f' {k}:{bookings[k]}' for k in sorted(bookings))
                expected+=f'\nbooking_count={len(bookings)}\n'
                run([mode,withdrawn,*values],expected)
    run(['message',0], 'dispatcher ready\n')
    for text in ['', 'created order 42', 'line\nbreak', 'customer \"quoted\"']:
        for attempt in [0,1,3,4,4294967295]:
            expected=(f'expired attempt={attempt}: {text}\n' if attempt > 3
                      else f'redelivered: {text}\n' if attempt > 0 else f'delivered: {text}\n')
            run(['message',attempt,text],expected)
    run(['message',1], 'retry needs message text\n',64)
    run(['route',2,1],'cannot omit more stops than the route contains\n',64)
    run(['detour',0,1],'unknown dispatch command\n',64)
    run(['detour',0,'x'],'unknown dispatch command\n',64)
    print(f'Dispatch: {count} route, scheduling, roster and message-policy checks passed')


if __name__=='__main__': main()
