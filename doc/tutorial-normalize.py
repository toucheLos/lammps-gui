#!/usr/bin/env python3
"""Reduce a LAMMPS input to its logical commands: drop comments and blanks,
join '&' continuations, and collapse whitespace.  Two scripts that agree here
give LAMMPS exactly the same instructions however they are laid out."""
import re, sys
out, buf = [], ""
for raw in open(sys.argv[1]):
    line = raw.split('#')[0].strip()
    if not line:
        continue
    if line.endswith('&'):
        buf += line[:-1].strip() + " "
        continue
    out.append(re.sub(r'\s+', ' ', (buf + line).strip()))
    buf = ""
if buf:
    out.append(re.sub(r'\s+', ' ', buf.strip()))
print("\n".join(out))
