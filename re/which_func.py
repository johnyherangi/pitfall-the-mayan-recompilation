#!/usr/bin/env python3
"""which_func.py <addr>...: the Ghidra function (re/index.tsv) containing each PITFALL.EXE address."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
rows=[]
for line in open(os.path.join(HERE, 'index.tsv')):
    p=line.rstrip('\n').split('\t'); rows.append((int(p[0],16),p[1],int(p[2])))
for a in sys.argv[1:]:
    a=int(a,16); hit=[r for r in rows if r[0]<=a<r[0]+r[2]]
    print(f"{a:#x}: " + (", ".join(f"{n}+{a-s:#x} (size {z})" for s,n,z in hit) if hit else "no function"))
