#!/usr/bin/env python3
"""Check measurement reports, iterator pages and sorting against Python values."""
import argparse
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    count=0
    def run(args,expected,status=0):
        nonlocal count
        result=subprocess.run([exe,*map(str,args)],capture_output=True,text=True,timeout=10)
        assert result.returncode==status and not result.stderr,(args,result)
        assert result.stdout==expected,(args,result.stdout,expected)
        count+=1
    for values in [[],[3,-2,7,0,-9],[1,2,3],[-2147483648,2147483647]]:
        negatives=[(i,v) for i,v in enumerate(values) if v<0]
        expected=f'count={len(values)} sum={sum(values)} any_negative={str(bool(negatives)).lower()} all_positive={str(all(v>0 for v in values)).lower()}\n'
        expected+=f'negative_index={negatives[0][0] if negatives else "none"}\nfirst_negative={negatives[0][1] if negatives else "none"}\n'
        if values: expected+=f'middle_input={values[len(values)//2]}\nlast_input={values[-1]}\n'
        run(['summary',*values],expected)
        for offset,limit in [(0,100),(1,2),(100,1),(0,0)]:
            expected='index square\n'+''.join(f'{i} {v*v}\n' for i,v in enumerate(values) if offset<=i<offset+limit)
            expected+='rejected'+''.join(f' {v}' for _,v in negatives)+'\n'
            run(['page',offset,limit,*values],expected)
    run(['sort',3,5,1,3], 'first=1\nlast=5\nminimum=1\nmaximum=5\ncontains=true sorted=true\nindex=1\nbinary_index=1\nvalues 1 3 5\n')
    run(['descending',3,5,1,3], 'first=5\nlast=1\nminimum=1\nmaximum=5\ncontains=true sorted=false\nindex=1\nvalues 5 3 1\n')
    run(['sort',3], 'first=none\nlast=none\nminimum=none\nmaximum=none\ncontains=false sorted=true\nindex=none\nbinary_index=none\nvalues\n')
    run(['inspect',9,3,1,2], 'first=3\nlast=2\nminimum=1\nmaximum=3\ncontains=false sorted=false\nindex=none\n')
    run(['schedule',8,12], 'slot hour\n1 8\n2 9\n3 10\n4 11\n')
    run(['schedule',8,8], 'slot hour\n')
    run(['schedule',12,8], 'hours must satisfy 0 <= first <= end <= 24\n',64)
    # Runs are the maximal non-decreasing slices; the first longest one is reported.
    for values in [[],[3,5,1,3,2],[1,2,3],[5,4,3,2,1],[2,2,1],[-2147483648,2147483647,0]]:
        runs=[];start=0
        for i in range(1,len(values)):
            if values[i]<values[i-1]:runs.append(values[start:i]);start=i
        if start<len(values):runs.append(values[start:])
        expected=''.join('run'+''.join(f' {v}' for v in r)+'\n' for r in runs)
        longest=max(range(len(runs)),key=lambda i:(len(runs[i]),-i))+1 if runs else 'none'
        run(['runs',*values],expected+f'runs={len(runs)} longest={longest}\n')
    # Both halves are searched concurrently; positions refer to the whole input.
    for wanted,values in [(3,[3,5,1,3,2,3]),(9,[1,2]),(2,[1,2]),(1,[]),(7,[7]),(5,[1,5,5,2,5])]:
        positions=[i for i,v in enumerate(values) if v==wanted]
        run(['find',wanted,*values],f'first={positions[0] if positions else "none"} count={len(positions)}\n')
    print(f'Statistics: {count} report, page, sorting, run, search and schedule checks passed')


if __name__=='__main__':main()
