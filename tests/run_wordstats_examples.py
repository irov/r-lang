#!/usr/bin/env python3
"""Check word statistics computed over borrowed command-line words."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
cases = [([], 'wordstats WORD...\n', 0),
         (['b', 'a', 'b'], 'words=3 distinct=2 top=b:2 longest=b:1 short=3 sizes=3/0/0 repeated=b:2 ranking=b:2,a:1\n', 0),
         (['alpha', 'beta', 'alpha', 'gamma'],
          'words=4 distinct=3 top=alpha:2 longest=alpha:5 short=0 sizes=0/4/0 repeated=alpha:2 '
          'ranking=alpha:2,beta:1,gamma:1\n', 0),
         (['Hello', 'hello'],
          'words=2 distinct=2 top=Hello:1 longest=Hello:5 short=0 sizes=0/2/0 repeated=- '
          'ranking=Hello:1,hello:1\n', 0),
         (['aa', 'b' * 20, 'aa'],
          'words=3 distinct=2 top=aa:2 longest=' + 'b' * 20 + ':20 short=2 sizes=2/0/1 repeated=aa:2 '
          'ranking=aa:2,' + 'b' * 20 + ':1\n', 0),
         ('the cat and the dog saw the cat outside'.split(),
          'words=9 distinct=6 top=the:3 longest=outside:7 short=8 sizes=8/1/0 repeated=the:3,cat:2 '
          'ranking=the:3,cat:2,and:1\n', 0),
         ('x y y z z z w'.split(),
          'words=7 distinct=4 top=z:3 longest=x:1 short=7 sizes=7/0/0 repeated=y:2,z:3 '
          'ranking=z:3,y:2,x:1\n', 0),
         (['x1'], '', 65), (['ok', 'no way'], '', 65), (['', 'a'], '', 65)]
for values, output, status in cases:
    result = subprocess.run([args.executable, *values], text=True, capture_output=True, timeout=20)
    assert (result.returncode, result.stdout, result.stderr) == (status, output, ''), (values, result)
print(f'Word statistics: {len(cases)} container and borrow checks passed')
