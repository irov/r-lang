#!/usr/bin/env python3
"""Compare unique and shared snapshot lifetimes for both reference-counting policies."""
import argparse
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True)
    exe=p.parse_args().executable
    for kind in ['rc','arc']:
        for policy in ['unique','shared']:
            for initial,revised in [(7,11),(-5,2147483647)]:
                r=subprocess.run([exe,kind,policy,str(initial),str(revised)],capture_output=True,text=True,timeout=10)
                expected=f'replaced version=1 value={initial}\nsame_snapshot=true owners=2 observers=2\nforeign_snapshot version=2 value={revised}\nsubscription={revised}\n'
                expected+=f'unwrapped version=2 value={revised}\nreader_keeps_alive=false\n' if policy=='unique' else f'still_shared value={revised}\nreader_keeps_alive=true\n'
                assert r.returncode==0 and not r.stderr and r.stdout==expected,(kind,policy,r,expected)
    print('Snapshots: eight rc/arc lifetime scenarios passed')


if __name__=='__main__':main()
