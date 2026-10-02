#!/usr/bin/env python3
"""Check UTC conversion, exact duration arithmetic and the text forms of time against independent\ncalculations and the Python standard library, and intervals by their lower bounds."""
import argparse
import datetime
import email.utils
import re
import subprocess
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    count=0
    def run(args,expected=None,status=0):
        nonlocal count
        result=subprocess.run([exe,*map(str,args)],capture_output=True,text=True,timeout=10)
        assert result.returncode==status and not result.stderr,(args,result)
        if expected is not None: assert result.stdout==expected,(args,result.stdout,expected)
        count+=1
        return result.stdout
    def calendar(seconds,nanoseconds):
        value=datetime.datetime.fromtimestamp(seconds,datetime.timezone.utc)
        return f'utc={value.year} {value.month} {value.day} {value.hour} {value.minute} {value.second} {nanoseconds}\nunix={seconds} {nanoseconds}\n'
    for seconds,nanoseconds,delta in [(0,0,0),(-1,999999999,0),(1709251199,0,1),(0,4,-86400)]:
        run(['shift',seconds,nanoseconds,delta],calendar(seconds+delta,nanoseconds))
    for parts in [(1970,1,1,0,0,0,0),(2000,2,29,12,34,56,999),(1969,12,31,23,59,59,0)]:
        stamp=int(datetime.datetime(*parts[:6],tzinfo=datetime.timezone.utc).timestamp())
        run(['utc',*parts],calendar(stamp,parts[-1]))
    for a,b in [(1250000000,2750000000),(-750000000,500000000),(0,0)]:
        left=divmod(a,10**9); right=divmod(b,10**9)
        for operation,result in [('add',a+b),('sub',a-b)]:
            seconds,nanos=divmod(result,10**9)
            run([operation,*left,*right],f'seconds={seconds} nanoseconds={nanos}\n')
        run(['compare',*left,*right],f'order={(a>b)-(a<b)}\n')
        seconds,nanos=divmod(a*-3,10**9)
        run(['mul',*left,-3],f'seconds={seconds} nanoseconds={nanos}\n')
    for args,message in [(['utc',1900,2,29,0,0,0,0],'invalid calendar or clock value\n'),
                         (['utc',2000,1,1,0,0,60,0],'invalid calendar or clock value\n'),
                         (['shift',0,10**9,0],'invalid calendar or clock value\n'),
                         (['add',0,10**9,0,0],'invalid or overflowing duration\n'),
                         (['add',2**63-1,0,1,0],'invalid or overflowing duration\n')]:
        run(args,message,65)
    before=int(time.time())
    now=run(['now'])
    after=int(time.time())
    match=re.search(r'unix=(-?\d+) (\d+)\n$',now)
    assert match and before<=int(match[1])<=after,now
    assert now==calendar(int(match[1]),int(match[2])),now
    for milliseconds in [0,2,15]:
        elapsed=run(['sleep',milliseconds])
        match=re.fullmatch(r'seconds=(\d+) nanoseconds=(\d+)\n',elapsed)
        assert match,elapsed
        ns=int(match[1])*10**9+int(match[2])
        assert ns>=milliseconds*10**6,(milliseconds,ns)
    # M21: RFC 3339 and HTTP dates against the Python standard library.
    for seconds,nanoseconds,digits in [(784111777,123456789,0),(784111777,123456789,3),
                                        (0,5,9),(-1,999999999,2),(951782400,0,1),
                                        (253402300799,999999999,9)]:
        moment=datetime.datetime(1970,1,1)+datetime.timedelta(seconds=seconds)
        fraction=('.'+f'{nanoseconds:09}'[:digits]) if digits else ''
        run(['rfc3339',seconds,nanoseconds,digits],moment.isoformat()+fraction+'Z\n')
        stamp=datetime.datetime.fromtimestamp(seconds,datetime.timezone.utc) if seconds>=0 else \
            datetime.datetime(1970,1,1,tzinfo=datetime.timezone.utc)+datetime.timedelta(seconds=seconds)
        run(['http',seconds],email.utils.format_datetime(stamp,usegmt=True)+'\n')
    for text,seconds,nanoseconds in [('1994-11-06T08:49:37.5+01:00',784108177,500000000),
                                     ('1994-11-06 08:49:37z',784111777,0),
                                     ('2024-02-29T23:59:59.999999999-00:30',1709252999,999999999)]:
        stamp=datetime.datetime.fromtimestamp(seconds,datetime.timezone.utc)
        run(['parse',text],f'unix={seconds} {nanoseconds}\nhttp={email.utils.format_datetime(stamp,usegmt=True)}\n')
    for text in ['Sun, 06 Nov 1994 08:49:37 GMT','Sunday, 06-Nov-94 08:49:37 GMT','Sun Nov  6 08:49:37 1994']:
        run(['parse',text],'unix=784111777 0\nrfc3339=1994-11-06T08:49:37Z\n')
    for text,name,index in [('1994-13-06T08:49:37Z','above_maximum',5),('1994-11-06T08:49:37','invalid_digit',19),
                            ('Mon, 06 Nov 1994 08:49:37 GMT','invalid_digit',0),
                            ('Sun, 06 Nov 1994 08:49:37 GMTX','trailing_character',29)]:
        output=run(['parse',text],None,65)
        assert f'name={name} ' in output and output.endswith(f' index={index}\n'),(text,output)
    run(['rfc3339',0,0,10],'invalid calendar or clock value\n',65)
    run(['rfc3339',253402300800,0,0],None,65)
    run(['ticks',0,10],'demo limit is 1 to 100 ticks of 1 to 1000 ms and a 5000 ms pause\n',64)
    # Intervals: numbers grow and no tick completes before its time; a pause of 125 ms after the
    # first tick of 50 ms makes the next tick late, so it is at least tick 3.
    for args,period,least_second in [(['ticks',3,20],20,2),(['ticks',4,50,125],50,3)]:
        output=run(args)
        match=re.fullmatch(r'ticks:((?: \d+)+)\nelapsed=(\d+) ms\n',output)
        assert match,output
        numbers=[int(value) for value in match[1].split()]
        assert len(numbers)==args[1] and numbers[0]>=1 and numbers[1]>=least_second,output
        assert all(left<right for left,right in zip(numbers,numbers[1:])),output
        assert int(match[2])>=numbers[-1]*period,output
    print(f'Clock: {count} UTC, duration, clock, suspension, date text and interval checks passed')


if __name__=='__main__':main()
