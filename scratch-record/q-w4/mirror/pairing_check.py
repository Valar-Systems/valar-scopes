# SCRATCH (map-server): rule (c)'s REVIEWER CHECK, from 5d.2: a package comparator must never take a section by FIRST of its type.
# Section types repeat (terrain is one section per level: [1,2,3,5,6,6,6,7]), so "the section of type t" is only defined when t occurs
# once. The allowed forms are: (1) a helper that filters by type and THROWS unless exactly one matches; (2) pairing by INDEX after
# asserting the two type sequences are equal.
# This check reads source only. Per file it prints every `sections.find(` (the refused form), with its line and the type literal it
# selects, and a count. A NEW comparator passes with a count of 0. For an OLDER tool, each listed lookup is sound only where the type
# census (terrain26.mts) shows that type once in every package the tool reads.
#   python -X utf8 -I mirror/pairing_check.py <file> [<file> ...]       exit 0 when every file's count is 0
import re, sys

total = 0
for f in sys.argv[1:]:
    hits = []
    for n, l in enumerate(open(f, encoding="utf-8"), 1):
        for m in re.finditer(r"sections\.find\(", l):
            t = re.search(r"type\s*===\s*(\w+)", l[m.end():m.end() + 80])
            hits.append(f"{n}:type {t.group(1) if t else '?'}")
    total += len(hits)
    print(f"{f}: first-of-type lookups {len(hits)}{(' -- ' + ', '.join(hits)) if hits else ''}")
print(f"PAIRING: {total} first-of-type lookup(s) in {len(sys.argv) - 1} file(s)")
sys.exit(0 if total == 0 else 1)
